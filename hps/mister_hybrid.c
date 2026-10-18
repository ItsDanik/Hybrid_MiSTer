// HPS side of a MiSTer hybrid core, see mister_hybrid.h.
//
// The FPGA side (hybrid/rtl/hybrid_host.sv) scans out one of three
// framebuffers every field, reloads the palette when its sequence number
// changes and publishes input state once per vblank.
//
// The shared memory is mapped uncached, which makes copying a frame into it
// slow (about 1ms per 64KB). A present thread on CPU1 (when the launcher
// allows it) does that copy so the game thread only copies into a cached
// staging buffer.

#define _GNU_SOURCE
#include "mister_hybrid.h"
#include "mister_font.h"

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define SHM_PHYS 0x30000000
#define SHM_SIZE 0x400000

#define CTRL_OFFSET 0x0
#define STATUS_OFFSET 0x40
#define AUDIO_PTR_OFFSET 0xC0
#define AUDIO_RING_OFFSET 0x10000
#define PALETTE_OFFSET 0x1000
#define PALETTE_SLOT_SIZE 0x400
// the message: {flags, sequence number, magic}, then 4 lines of 80 bytes
#define MSG_OFFSET 0x1800
#define MSG_MAGIC 0x534D484D // "MHMS"
#define MSG_LINE 80
#define FB_OFFSET 0x100000
#define FB_SIZE 0x100000
#define FB_COUNT 3

// status: the mode is interlaced; the field that was on the screen had the
// even rows; the game has the screen (host version 9). Bit 16 was "800x600
// and 1024x768 are available" up to host version 12.
#define STATUS_LACE (1u << 9)
#define STATUS_FIELD_EVEN (1u << 11)
#define STATUS_GAME (1u << 19)
// a message is shown; the core's own (host version 13)
#define STATUS_MSG (1u << 20)
#define STATUS_ALONE (1u << 21)

static const struct {
    short width, height;
    unsigned char version; // the host version that has the mode, 0: none
} modes[MH_MODE_COUNT] = {
    { 320, 200, 4 }, { 640, 200, 4 }, { 640, 400, 5 }, { 320, 240, 6 },
    { 640, 480, 6 }, { 800, 600, 0 }, { 1024, 768, 0 }, { 640, 240, 8 },
};

#define CTRL_MAGIC 0x4259484D   // "MHYB"
#define STATUS_MAGIC 0x5359484D // "MHYS"

static int mem_fd = -1;
static volatile uint8_t* shm;
static volatile uint32_t* ctrl;
static volatile uint32_t* status;
static char core_name[64];
static int shm_is_file;

static int fb_format;
static int fb_mode;
static int fb_current;
static int palette_slot;
static uint32_t palette_seq;
static int ctrl_enabled;
static int screen_ours; // the core's own picture is gone
static int menu_mask;
static int audio_enabled;
// set once the core is gone: another core may own the memory now, never write to it again
static volatile int detached;
static int reloaded; // detached because the core started over

static int cpu1_allowed;

// present thread: FIFO of up to 2 staged frames
static pthread_t present_thread;
static int present_threaded;
static int present_running;
static pthread_mutex_t present_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t present_cond = PTHREAD_COND_INITIALIZER;
static uint8_t* staging[2];
static int staging_next;
static int staging_direct = -1; // handed out by MH_FrameBuffer()
static int queue[2];
static uint32_t queue_at[2]; // the field each queued frame is to be shown at, 0: at once
static int queue_head;
static int queue_count;
static uint32_t present_at; // MH_PresentAtField() for the next frame presented
// serialises control block updates between the game, present and audio threads
static pthread_mutex_t ctrl_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t alive_frame;
static struct timespec alive_time;

static uint32_t audio_wr;

// FM chip: ring of register writes at FM_RING_OFFSET, 64 bits each
// {16'b0, reg, value, audio frame}
#define FM_RING_OFFSET 0x20000
static int fm_enabled;
static int fm_volume = 32;
static uint32_t fm_wr;

static void write_ctrl(void) {
    // Palette/framebuffer writes must land before the FPGA can see the new control block
    pthread_mutex_lock(&ctrl_lock);
    __sync_synchronize();
    ctrl[2] = palette_seq;
    ctrl[3] = fm_wr;
    ctrl[1] = fb_current | (fb_format << 8) | (fb_mode << 12) | (palette_slot << 16) | (audio_enabled << 24)
        | ((uint32_t)fm_volume << 17) | ((uint32_t)(fm_enabled && audio_enabled) << 25) | ((uint32_t)menu_mask << 28);
    ctrl[0] = ctrl_enabled ? CTRL_MAGIC : 0;
    __sync_synchronize();
    pthread_mutex_unlock(&ctrl_lock);
}

static void sleep_ms(int ms) {
    struct timespec ts = { 0, ms * 1000000 };
    nanosleep(&ts, NULL);
}

int MH_Mode(void) {
    return fb_mode;
}

int MH_ModeWidth(int mode) {
    return mode >= 0 && mode < MH_MODE_COUNT ? modes[mode].width : 0;
}

int MH_ModeHeight(int mode) {
    return mode >= 0 && mode < MH_MODE_COUNT ? modes[mode].height : 0;
}

int MH_Width(void) {
    return modes[fb_mode].width;
}

int MH_Height(void) {
    return modes[fb_mode].height;
}

int MH_ModeAvailable(int mode) {
    return MH_IsOpen() && mode >= 0 && mode < MH_MODE_COUNT && modes[mode].version != 0
        && status[3] >= modes[mode].version;
}

static int frame_bytes(void) {
    return MH_Width() * MH_Height() * (fb_format == MH_FORMAT_RGB565 ? 2 : 1);
}

// The framebuffers, the same for every video mode
static volatile uint8_t* framebuffer(int index) {
    return shm + FB_OFFSET + index * FB_SIZE;
}

// Bytes of a frame of a mode in the larger pixel format
static int mode_bytes(int mode) {
    return modes[mode].width * modes[mode].height * 2;
}

static void clear_framebuffer(int index, int bytes) {
    memset((void*)framebuffer(index), 0, bytes);
}

// 1 if /tmp/CORENAME names our core (or there is nothing to compare)
static int corename_matches(void) {
    char name[64] = { 0 };
    size_t len = strlen(core_name);
    FILE* f;

    if (len == 0 || shm_is_file) {
        return 1;
    }
    f = fopen("/tmp/CORENAME", "r");
    if (f == NULL) {
        return 1;
    }
    if (fgets(name, sizeof(name), f) == NULL) {
        name[0] = 0;
    }
    fclose(f);
    return strncmp(name, core_name, len) == 0;
}

int MH_Open(void) {
    const char* env;
    const char* shm_file = getenv("MISTER_HYBRID_SHM");
    cpu_set_t cpus;
    uint32_t frame;
    int i;

    if (shm != NULL) {
        return !detached;
    }
    env = getenv("MISTER_HYBRID_CORE");
    snprintf(core_name, sizeof(core_name), "%s", env ? env : "");

    shm_is_file = shm_file != NULL && shm_file[0] != 0;
    mem_fd = open(shm_is_file ? shm_file : "/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        fprintf(stderr, "mister: cannot open %s, running headless\n", shm_is_file ? shm_file : "/dev/mem");
        return 0;
    }
    shm = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, shm_is_file ? 0 : SHM_PHYS);
    if (shm == MAP_FAILED) {
        fprintf(stderr, "mister: cannot map shared memory, running headless\n");
        close(mem_fd);
        mem_fd = -1;
        shm = NULL;
        return 0;
    }
    ctrl = (volatile uint32_t*)(shm + CTRL_OFFSET);
    status = (volatile uint32_t*)(shm + STATUS_OFFSET);

    // The core rewrites the status block every vblank; a stale block from an
    // earlier session has a frozen frame counter.
    frame = status[1];
    for (i = 0; i < 20 && status[1] == frame; i++) {
        sleep_ms(10);
    }
    if (status[0] != STATUS_MAGIC || status[1] == frame || !corename_matches()) {
        fprintf(stderr, "mister: %s core not running, running headless\n", core_name[0] ? core_name : "hybrid");
        munmap((void*)shm, SHM_SIZE);
        shm = NULL;
        close(mem_fd);
        mem_fd = -1;
        return 0;
    }
    detached = 0;
    reloaded = 0;
    ctrl_enabled = 0;
    screen_ours = 0;
    audio_enabled = 0;
    fm_enabled = 0;
    fm_wr = 0;
    fb_mode = MH_MODE_320x200;
    // The core reloads the palette when its sequence number changes, and it
    // may know the number an earlier program ended with: count on from there
    menu_mask = 0;
    if (ctrl[0] == CTRL_MAGIC) {
        palette_seq = ctrl[2];
        palette_slot = (ctrl[1] >> 16) & 1;
    }
    alive_time.tv_sec = 0;
    for (i = 0; i < FB_COUNT; i++) {
        // all of it: whatever ran before us may have used a larger mode
        clear_framebuffer(i, FB_SIZE);
    }
    write_ctrl();

    // With both CPUs allowed (taskset 0x03 in the launcher) the game thread
    // gets CPU0 to itself: it has the better DDR3 bandwidth and Main_MiSTer
    // lives on CPU1. Helper threads go to CPU1 (MH_PinThreadToCPU1).
    cpu1_allowed = 0;
    if (sched_getaffinity(0, sizeof(cpus), &cpus) == 0 && CPU_ISSET(0, &cpus) && CPU_ISSET(1, &cpus)) {
        cpu1_allowed = 1;
        CPU_ZERO(&cpus);
        CPU_SET(0, &cpus);
        sched_setaffinity(0, sizeof(cpus), &cpus);
    }
    printf("mister: %s core found (host version %u)%s\n", core_name[0] ? core_name : "hybrid", status[3],
        cpu1_allowed ? ", helper threads on CPU1" : "");
    return 1;
}

int MH_PinThreadToCPU1(void) {
    cpu_set_t cpu1;

    if (!cpu1_allowed) {
        return 0;
    }
    CPU_ZERO(&cpu1);
    CPU_SET(1, &cpu1);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu1), &cpu1) == 0;
}

static void stop_present_thread(void) {
    if (!present_threaded) {
        return;
    }
    pthread_mutex_lock(&present_lock);
    present_running = 0;
    pthread_cond_broadcast(&present_cond);
    pthread_mutex_unlock(&present_lock);
    pthread_join(present_thread, NULL);
    present_threaded = 0;
    queue_count = 0;
    staging_direct = -1;
}

void MH_Close(void) {
    stop_present_thread();
    if (shm != NULL) {
        if (!detached && corename_matches()) {
            // back to the core's own picture, audio off. Not once another
            // core is loaded: the memory is its own then
            ctrl_enabled = 0;
            audio_enabled = 0;
            fm_enabled = 0;
            fm_wr = 0;
            write_ctrl();
        }
        munmap((void*)shm, SHM_SIZE);
        shm = NULL;
    }
    if (mem_fd >= 0) {
        close(mem_fd);
        mem_fd = -1;
    }
}

int MH_IsOpen(void) {
    return shm != NULL && !detached;
}

const char* MH_CoreName(void) {
    return core_name;
}

// The set name in /tmp/CORENAME when it is ours (the core's name or
// <core>_<game>), else "": what Main_MiSTer names the core's files after
static void corename_set(char* out, size_t size) {
    const char* core = getenv("MISTER_HYBRID_CORE");
    char name[64] = { 0 };
    size_t len;
    FILE* f;

    out[0] = 0;
    core = core ? core : "";
    len = strlen(core);
    if (len == 0 || (f = fopen("/tmp/CORENAME", "r")) == NULL) {
        return;
    }
    if (fgets(name, sizeof(name), f) != NULL) {
        name[strcspn(name, "\r\n")] = 0;
        if (strncmp(name, core, len) == 0 && (name[len] == 0 || name[len] == '_')) {
            snprintf(out, size, "%s", name);
        }
    }
    fclose(f);
}

const char* MH_MappingName(void) {
    static char name[64];

    corename_set(name, sizeof(name));
    return name[0] != 0 ? name : MH_SetName();
}

const char* MH_SetName(void) {
    static char set_name[64];
    const char* core = getenv("MISTER_HYBRID_CORE");
    const char* env = getenv("MISTER_HYBRID_SET");
    char name[64] = { 0 };
    size_t len;
    FILE* f;

    if (set_name[0] != 0) {
        return set_name;
    }
    core = core ? core : "";
    len = strlen(core);
    snprintf(set_name, sizeof(set_name), "%s", core);
    if (env != NULL && env[0] != 0) {
        snprintf(set_name, sizeof(set_name), "%s", env);
    } else if (len != 0 && (f = fopen("/tmp/CORENAME", "r")) != NULL) {
        if (fgets(name, sizeof(name), f) != NULL) {
            name[strcspn(name, "\r\n")] = 0;
            // ours: the core's name or <core>_<game>
            if (strncmp(name, core, len) == 0 && (name[len] == 0 || name[len] == '_')) {
                snprintf(set_name, sizeof(set_name), "%s", name);
            }
        }
        fclose(f);
    }
    return set_name;
}

// The OSD's FPS Counter (MH_OSD_FPS): the frames presented in the last second,
// drawn into the copy of the frame that the core shows, 8 pixels in from the
// corner (a television hides the edge). With a palette: its lightest colour
// on its darkest.
static void fps_stamp(volatile uint8_t* dst, int row, int height) {
    static struct timespec since;
    static int count;
    static int shown = -1;
    static uint32_t pal_seq = ~0u;
    static uint8_t ink8, paper8;
    struct timespec now;
    char text[8];
    double elapsed;
    int width = MH_Width();
    int bpp = row / width;
    int sx = width >= 640 ? 2 : 1;
    int sy = height >= 400 ? 2 : 1;
    int n, c, x, y, w;
    static int forced = -2; // MISTER_HYBRID_FPS=0|1: the counter off or on whatever the OSD says (screenshots)

    if (forced == -2) {
        forced = getenv("MISTER_HYBRID_FPS") ? atoi(getenv("MISTER_HYBRID_FPS")) != 0 : -1;
    }
    if (forced == 0 || (forced < 0 && !MH_OSD_FPS(MH_OSDStatus()))) {
        shown = -1;
        count = 0;
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (shown < 0 && count == 0) {
        since = now;
    }
    count++;
    elapsed = (double)(now.tv_sec - since.tv_sec) + (double)(now.tv_nsec - since.tv_nsec) * 1e-9;
    if (elapsed >= 1.0) {
        shown = (int)((count - 1) / elapsed + 0.5);
        count = 1;
        since = now;
    }
    if (shown < 0) {
        return;
    }
    if (bpp == 1 && pal_seq != palette_seq) {
        const volatile uint32_t* pal = (const volatile uint32_t*)(shm + PALETTE_OFFSET + palette_slot * PALETTE_SLOT_SIZE);
        int light = -1, dark = 1 << 30;

        for (c = 0; c < 256; c++) {
            uint32_t p = pal[c];
            int v = (int)((p >> 16) & 255) * 2 + (int)((p >> 8) & 255) * 5 + (int)(p & 255);

            if (v > light) { light = v; ink8 = (uint8_t)c; }
            if (v < dark) { dark = v; paper8 = (uint8_t)c; }
        }
        pal_seq = palette_seq;
    }
    n = snprintf(text, sizeof text, "%d", shown > 999 ? 999 : shown);
    w = n * 8 + 2;
    if ((8 + w) * sx > width || 18 * sy > height) {
        return;
    }
    for (y = 0; y < 10 * sy; y++) {
        volatile uint8_t* line = dst + (8 * sy + y) * row;

        for (x = 0; x < w * sx; x++) {
            int gx = x / sx - 1, gy = y / sy - 1;
            int on = 0;

            if (gx >= 0 && gx < n * 8 && gy >= 0 && gy < 8) {
                on = (mh_font[text[gx / 8] - 32][gy] >> (gx & 7)) & 1;
            }
            if (bpp == 2) {
                ((volatile uint16_t*)line)[8 * sx + x] = on ? 0xFFE0 : 0x0000;
            } else {
                line[8 * sx + x] = on ? ink8 : paper8;
            }
        }
    }
}


// MISTER_HYBRID_PACE=1: every 300 frames, on stderr, how the frames came to
// the core. It shows the newest at each vblank, so a frame handed over in the
// same field as the one before means that one was never shown, and one two
// fields after the one before (where one was meant) that the frame before was
// shown twice: the stutter of a game that is at 60 frames per second by its
// own count. Also how long after the start of a field (as the game saw it in
// MH_WaitFrame() or MH_FieldCounter()) frames were handed over, at most.
static int pace_on = -1;
static uint32_t pace_field_seen;
static struct timespec pace_field_time;

static void pace_field(uint32_t field) {
    if (pace_on && field != pace_field_seen) {
        pace_field_seen = field;
        clock_gettime(CLOCK_MONOTONIC, &pace_field_time);
    }
}

static void pace_flip(uint32_t field) {
    static uint32_t last;
    static unsigned frames, same, next, later;
    static double latest;
    struct timespec now;

    if (pace_on < 0) {
        pace_on = getenv("MISTER_HYBRID_PACE") != NULL;
    }
    if (!pace_on) {
        return;
    }
    if (frames || last) {
        if (field == last) {
            same++;
        } else if (field - last == 1) {
            next++;
        } else {
            later++;
        }
    }
    last = field;
    if (field == pace_field_seen) {
        double ms;

        clock_gettime(CLOCK_MONOTONIC, &now);
        ms = (now.tv_sec - pace_field_time.tv_sec) * 1e3 + (now.tv_nsec - pace_field_time.tv_nsec) * 1e-6;
        if (ms > latest) {
            latest = ms;
        }
    }
    if (++frames == 300) {
        fprintf(stderr, "mister: 300 frames to the core: in the same field as the one before %u, the next %u, later %u; "
                "at most %.1fms into a field\n", same, next, later, latest);
        frames = same = next = later = 0;
        latest = 0;
    }
}

// Copy a frame into the next framebuffer and flip to it
static void present_to_fpga(const uint8_t* pixels, int pitch, uint32_t at) {
    volatile uint8_t* dst;
    int height = MH_Height();
    int row = frame_bytes() / height;
    int next;
    int y;
    int i;

    // Triple buffering. Never write into the buffer that is being scanned out:
    // with two frames submitted within one vblank the FPGA may still show it.
    next = (fb_current + 1) % FB_COUNT;
    for (i = 0; i < 50 && (status[2] & 0xff) == (uint32_t)next; i++) {
        sleep_ms(1);
    }

    dst = framebuffer(next);
    if (pitch == row) {
        memcpy((void*)dst, pixels, row * height);
    } else {
        for (y = 0; y < height; y++) {
            memcpy((void*)(dst + y * row), pixels + y * pitch, row);
        }
    }
    fps_stamp(dst, row, height);
    // MH_PresentAtField(): the core takes the newest frame at every vblank, so
    // this one is handed over in the field before its own, not earlier (the
    // frame before it would never be shown). Interlaced, frames are taken at
    // every second vblank: at once.
    if (at != 0 && !(status[2] & STATUS_LACE)) {
        for (i = 0; i < 100 && MH_IsOpen() && (int32_t)(status[1] - (at - 1)) < 0; i++) {
            sleep_ms(1);
        }
    }
    fb_current = next;
    if (fb_format == MH_FORMAT_RGB565) {
        ctrl_enabled = 1;
    }
    write_ctrl();
    pace_flip(status[1]);
}

static void* present_thread_main(void* arg) {
    int slot;
    uint32_t at;

    (void)arg;
    MH_PinThreadToCPU1();
    pthread_mutex_lock(&present_lock);
    for (;;) {
        while (queue_count == 0 && present_running) {
            pthread_cond_wait(&present_cond, &present_lock);
        }
        if (queue_count == 0) {
            break;
        }
        slot = queue[queue_head];
        at = queue_at[queue_head];
        pthread_mutex_unlock(&present_lock);

        if (MH_IsOpen()) {
            present_to_fpga(staging[slot], frame_bytes() / MH_Height(), at);
        }

        pthread_mutex_lock(&present_lock);
        queue_head = (queue_head + 1) % 2;
        queue_count--;
        pthread_cond_broadcast(&present_cond);
    }
    pthread_mutex_unlock(&present_lock);
    return NULL;
}

static void start_present_thread(void) {
    // only worth it with a second CPU to run on
    if (present_threaded || !cpu1_allowed) {
        return;
    }
    if (staging[0] == NULL) {
        staging[0] = malloc(MH_MAX_WIDTH * MH_MAX_HEIGHT * 2);
        staging[1] = malloc(MH_MAX_WIDTH * MH_MAX_HEIGHT * 2);
    }
    present_running = 1;
    if (pthread_create(&present_thread, NULL, present_thread_main, NULL) == 0) {
        present_threaded = 1;
    } else {
        present_running = 0;
    }
}

// Wait until all staged frames reached the FPGA
static void present_flush(void) {
    if (!present_threaded) {
        return;
    }
    pthread_mutex_lock(&present_lock);
    while (queue_count > 0) {
        pthread_cond_wait(&present_cond, &present_lock);
    }
    pthread_mutex_unlock(&present_lock);
}

// The core fades its own picture out before it shows the first of ours (host
// version 9), which takes 0.7 seconds, and from host version 13 it first
// plays its intro to the end (4.1 seconds from its start). Waiting for that
// here is what keeps the game from showing its first frames to nobody.
static void wait_for_screen(void) {
    int i;

    if (screen_ours || !ctrl_enabled) {
        return;
    }
    for (i = 0; i < 700 && MH_IsOpen() && status[3] >= 9 && !(status[2] & STATUS_GAME); i++) {
        sleep_ms(10);
    }
    screen_ours = 1;
    // a message ("Loading...") was shown until now
    MH_ClearMessage();
}

// Wrap `text` into lines of MH_MSG_COLS: at '\n', and at the last space that
// fits (a word longer than a line is broken). Empty lines are left out.
// Returns the number of lines needed; the first `rows` go to `lines`.
static int wrap_message(const char* text, char lines[][MH_MSG_COLS + 1], int rows) {
    int count = 0;

    while (*text != 0) {
        int len = (int)strcspn(text, "\n");
        int i;

        if (len > MH_MSG_COLS) {
            len = MH_MSG_COLS;
            while (len > 0 && text[len] != ' ') {
                len--;
            }
            if (len == 0) {
                len = MH_MSG_COLS;
            }
        }
        if (len > 0) {
            if (count < rows) {
                for (i = 0; i < len; i++) {
                    unsigned char c = (unsigned char)text[i];
                    lines[count][i] = c == '\t' ? ' ' : (c < 32 || c > 126) ? '?' : (char)c;
                }
                lines[count][len] = 0;
            }
            count++;
        }
        text += len;
        while (*text == ' ' || *text == '\n') {
            text++;
        }
    }
    return count;
}

// The message block: 4 lines, and in its first word the flags (bit 0 error,
// bit 1 list, bits 2..3 the line selected), a new sequence number and the
// magic, last. A game that has the screen gives it back.
static void put_message(char lines[MH_MSG_ROWS][MH_MSG_COLS + 1], uint32_t flags) {
    volatile uint32_t* msg = (volatile uint32_t*)(shm + MSG_OFFSET);
    volatile uint8_t* chars = shm + MSG_OFFSET + 8;
    uint32_t seq;
    int i, j;

    for (i = 0; i < MH_MSG_ROWS; i++) {
        for (j = 0; j < MSG_LINE; j++) {
            chars[i * MSG_LINE + j] = j < MH_MSG_COLS ? (uint8_t)lines[i][j] : 0;
        }
    }
    // another sequence number than the one before, also another program's
    seq = (msg[0] == MSG_MAGIC ? msg[1] + 1 : (uint32_t)time(NULL)) & 0xFFFF;
    __sync_synchronize();
    msg[1] = seq | flags << 16;
    msg[0] = MSG_MAGIC;
    __sync_synchronize();
    // the core shows its picture only while no game has the screen
    if (ctrl_enabled) {
        present_flush();
        ctrl_enabled = 0;
        write_ctrl();
        screen_ours = 0;
    }
}

int MH_ShowMessage(const char* text, int flags) {
    char lines[MH_MSG_ROWS][MH_MSG_COLS + 1];
    int count;

    memset(lines, 0, sizeof(lines));
    count = wrap_message(text, lines, MH_MSG_ROWS);
    if (count > MH_MSG_ROWS) {
        int len = (int)strlen(lines[MH_MSG_ROWS - 1]);
        if (len > MH_MSG_COLS - 3) {
            len = MH_MSG_COLS - 3;
        }
        while (len > 0 && (lines[MH_MSG_ROWS - 1][len - 1] == '.' || lines[MH_MSG_ROWS - 1][len - 1] == ' ')) {
            len--;
        }
        strcpy(lines[MH_MSG_ROWS - 1] + len, "...");
    }
    printf("mister: message%s: %s\n", count > MH_MSG_ROWS ? " (cut, it needs more than 4 lines)" : "", text);
    if (MH_IsOpen() && status[3] >= 13) {
        put_message(lines, (flags & MH_MSG_ERROR) ? 1 : 0);
    }
    return count;
}

void MH_ShowList(const char* const* items, int count, int top, int selected) {
    char lines[MH_MSG_ROWS][MH_MSG_COLS + 1];
    int i, j;

    if (!MH_IsOpen() || status[3] < 13) {
        return;
    }
    memset(lines, 0, sizeof(lines));
    for (i = 0; i < MH_MSG_ROWS; i++) {
        memset(lines[i], ' ', MH_MSG_COLS);
        for (j = 0; top + i < count && j < MH_MSG_COLS - 2 && items[top + i][j] != 0; j++) {
            unsigned char c = (unsigned char)items[top + i][j];
            lines[i][j] = (c < 32 || c > 126) ? '?' : (char)c;
        }
    }
    // more above, more below: in the last column, which the core dims
    if (top > 0) {
        lines[0][MH_MSG_COLS - 1] = '^';
    }
    if (top + MH_MSG_ROWS < count) {
        lines[MH_MSG_ROWS - 1][MH_MSG_COLS - 1] = 'v';
    }
    put_message(lines, 2 | (uint32_t)((selected - top) & 3) << 2);
}

void MH_ClearMessage(void) {
    if (MH_IsOpen() && ((volatile uint32_t*)(shm + MSG_OFFSET))[0] == MSG_MAGIC) {
        ((volatile uint32_t*)(shm + MSG_OFFSET))[0] = 0;
        __sync_synchronize();
    }
}

int MH_HostVersion(void) {
    return MH_IsOpen() ? (int)status[3] : 0;
}

int MH_MessageShown(void) {
    if (!MH_IsOpen() || status[3] < 13) {
        return 0;
    }
    return (status[2] & STATUS_MSG) ? 1 : (status[2] & STATUS_ALONE) ? 2 : 0;
}

// Clear every framebuffer. The buffer on screen is cleared once the core
// shows a cleared one. `next_mode` is the mode that is set next.
static void blank_framebuffers(int next_mode) {
    int bytes = mode_bytes(fb_mode);
    int i, n;

    // what the next mode reads has to be black as far as its frames go
    if (mode_bytes(next_mode) > bytes) {
        bytes = mode_bytes(next_mode);
    }
    for (n = 1; n < FB_COUNT; n++) {
        clear_framebuffer((fb_current + n) % FB_COUNT, bytes);
    }
    i = fb_current;
    fb_current = (fb_current + 1) % FB_COUNT;
    write_ctrl();
    for (n = 0; n < 50 && ctrl_enabled && (status[2] & 0xff) == (uint32_t)i; n++) {
        sleep_ms(1);
    }
    clear_framebuffer(i, bytes);
}

void MH_SetFormat(int format) {
    if (!MH_IsOpen() || format == fb_format) {
        return;
    }
    present_flush();
    // A game that changes the format while it runs (8 bit for one screen,
    // RGB565 for another) goes through black (pixel 0), as in MH_SetMode():
    // not through the old frames read in the new format, nor the core's picture
    blank_framebuffers(fb_mode);
    fb_format = format;
    write_ctrl();
}

int MH_SetMode(int mode) {

    if (!MH_IsOpen() || mode < 0 || mode >= MH_MODE_COUNT) {
        return 0;
    }
    if (mode == fb_mode) {
        return 1;
    }
    if (!MH_ModeAvailable(mode)) {
        return 0;
    }
    present_flush();
    // The rows of the framebuffers are of another length in the new mode: go
    // through black (pixel 0) instead of showing the old frames torn apart.
    blank_framebuffers(mode);
    fb_mode = mode;
    write_ctrl();
    return 1;
}

void MH_Present(const void* pixels, int pitch) {
    const uint8_t* src = pixels;
    uint8_t* dst;
    int height = MH_Height();
    int row = frame_bytes() / height;
    int slot;
    int y;

    if (!MH_IsOpen()) {
        return;
    }
    start_present_thread();
    if (!present_threaded) {
        present_to_fpga(src, pitch, present_at);
        present_at = 0;
        wait_for_screen();
        return;
    }

    pthread_mutex_lock(&present_lock);
    if (staging_direct >= 0) {
        // the buffer of MH_FrameBuffer() is taken back
        staging_next = staging_direct;
        staging_direct = -1;
    }
    while (queue_count == 2) {
        pthread_cond_wait(&present_cond, &present_lock);
    }
    slot = staging_next;
    staging_next ^= 1;
    pthread_mutex_unlock(&present_lock);

    dst = staging[slot];
    if (pitch == row) {
        memcpy(dst, src, row * height);
    } else {
        for (y = 0; y < height; y++) {
            memcpy(dst + y * row, src + y * pitch, row);
        }
    }

    pthread_mutex_lock(&present_lock);
    queue[(queue_head + queue_count) % 2] = slot;
    queue_at[(queue_head + queue_count) % 2] = present_at;
    present_at = 0;
    queue_count++;
    pthread_cond_signal(&present_cond);
    pthread_mutex_unlock(&present_lock);
    if (!screen_ours) {
        present_flush();
        wait_for_screen();
    }
}

void* MH_FrameBuffer(void) {
    if (!MH_IsOpen()) {
        return NULL;
    }
    start_present_thread();
    if (!present_threaded) {
        return NULL;
    }
    pthread_mutex_lock(&present_lock);
    if (staging_direct < 0) {
        // the slot after the queued ones is free once fewer than two are queued
        while (queue_count == 2) {
            pthread_cond_wait(&present_cond, &present_lock);
        }
        staging_direct = staging_next;
        staging_next ^= 1;
    }
    pthread_mutex_unlock(&present_lock);
    return staging[staging_direct];
}

void MH_PresentBuffer(void) {
    if (!MH_IsOpen() || !present_threaded || staging_direct < 0) {
        return;
    }
    pthread_mutex_lock(&present_lock);
    queue[(queue_head + queue_count) % 2] = staging_direct;
    queue_at[(queue_head + queue_count) % 2] = present_at;
    present_at = 0;
    queue_count++;
    staging_direct = -1;
    pthread_cond_signal(&present_cond);
    pthread_mutex_unlock(&present_lock);
    if (!screen_ours) {
        present_flush();
        wait_for_screen();
    }
}

void MH_SetPalette(const uint32_t* rgb) {
    volatile uint32_t* pal;
    int i;

    if (!MH_IsOpen()) {
        return;
    }
    // Frames queued before this palette change must reach the FPGA first
    present_flush();

    // Two palette slots: fill the one the FPGA is not using, then switch
    palette_slot ^= 1;
    pal = (volatile uint32_t*)(shm + PALETTE_OFFSET + palette_slot * PALETTE_SLOT_SIZE);
    for (i = 0; i < 256; i++) {
        pal[i] = rgb[i] & 0xffffff;
    }
    palette_seq++;
    ctrl_enabled = 1;
    write_ctrl();
    wait_for_screen();
}

void MH_PresentAtField(uint32_t field) {
    // MISTER_HYBRID_AHEAD=0: frames shown as soon as they come, for comparing
    static int ahead = -1;

    if (ahead < 0) {
        ahead = !getenv("MISTER_HYBRID_AHEAD") || atoi(getenv("MISTER_HYBRID_AHEAD")) != 0;
    }
    present_at = ahead ? field : 0;
}

int MH_Interlaced(void) {
    return MH_IsOpen() && (status[2] & STATUS_LACE) != 0;
}

uint32_t MH_FieldCounter(void) {
    if (!MH_IsOpen()) {
        return 0;
    }
    pace_field(status[1]);
    return status[1];
}

void MH_WaitField(uint32_t field) {
    int i;

    // at most 4 fields, in case the core goes away
    for (i = 0; i < 70 && MH_IsOpen() && (int32_t)(status[1] - field) <= 0; i++) {
        sleep_ms(1);
    }
}

int MH_FrameTaken(uint32_t field) {
    uint32_t state;

    if (!MH_IsOpen()) {
        return 1;
    }
    if ((int32_t)(status[1] - field) <= 0) {
        return 0;
    }
    // interlaced, the core takes a frame in the vblank after the field with the odd rows
    state = status[2];
    return !(state & STATUS_LACE) || !(state & STATUS_FIELD_EVEN);
}

void MH_WaitFrame(uint32_t field) {
    int i;

    // at most 4 fields, in case the core goes away
    for (i = 0; i < 70 && !MH_FrameTaken(field); i++) {
        sleep_ms(1);
    }
    if (MH_IsOpen()) {
        pace_field(status[1]);
    }
}

void MH_SetMenuMask(int mask) {
    if (!MH_IsOpen()) {
        return;
    }
    menu_mask = mask & 0xf;
    write_ctrl();
}

static long elapsed_ms(const struct timespec* since) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - since->tv_sec) * 1000 + (now.tv_nsec - since->tv_nsec) / 1000000;
}

int MH_CheckAlive(void) {
    if (!MH_IsOpen()) {
        return !detached;
    }
    if (alive_time.tv_sec == 0) {
        alive_frame = status[1];
        clock_gettime(CLOCK_MONOTONIC, &alive_time);
        return 1;
    }
    if (elapsed_ms(&alive_time) < 500) {
        return 1;
    }
    // Another core may use the same memory: stop touching it as soon as ours is gone.
    // A field counter that went back is our core loaded again: it knows nothing
    // of this game any more (video mode, palette, audio), which has to start over.
    if (status[0] != STATUS_MAGIC || status[1] == alive_frame || status[1] < alive_frame || !corename_matches()) {
        reloaded = status[0] == STATUS_MAGIC && status[1] < alive_frame;
        detached = 1;
        return 0;
    }
    alive_frame = status[1];
    clock_gettime(CLOCK_MONOTONIC, &alive_time);
    return 1;
}

int MH_CoreReloaded(void) {
    // caught while it is being loaded, the counter stands still and the name is all there is
    return detached && (reloaded || (core_name[0] != 0 && !shm_is_file && corename_matches()));
}

int MH_OSDOpen(void) {
    // bit 22 of the status word 1, host version 14
    return MH_IsOpen() && status[3] >= 14 && (status[2] >> 22 & 1);
}

int mh_osd_hold(int* held, int pressed) {
    if (MH_OSDOpen()) {
        *held = 1;
        return 0;
    }
    if (!pressed) {
        *held = 0;
    }
    return *held ? 0 : pressed;
}

// How far from the middle a stick is still let go
#define STICK_REST 24

void MH_ReadInput(mh_input* input) {
    // What was held when the OSD closed, until it is let go; the mouse's
    // movement while it was open, taken off its accumulators for good
    static uint32_t held_joy[2], held_keys[16];
    static int held_mouse, held_axis[2][4];
    static int32_t mouse_off[2], last_mouse[2];
    static int16_t wheel_off, last_wheel;
    int8_t* axes[4];
    int open;
    int i, k;

    memset(input, 0, sizeof(*input));
    if (!MH_IsOpen()) {
        return;
    }
    open = MH_OSDOpen();
    input->frame = status[1];
    input->joystick[0] = status[4];
    input->joystick[1] = status[5];
    for (i = 0; i < 2; i++) {
        uint32_t analog = status[6 + i];
        input->analog_lx[i] = (int8_t)(analog & 0xff);
        input->analog_ly[i] = (int8_t)((analog >> 8) & 0xff);
        input->analog_rx[i] = (int8_t)((analog >> 16) & 0xff);
        input->analog_ry[i] = (int8_t)((analog >> 24) & 0xff);
    }
    input->osd_status = status[8] | ((uint64_t)status[9] << 32);
    input->mouse_x = (int32_t)status[10];
    input->mouse_y = (int32_t)status[11];
    input->mouse_buttons = status[12] & 7;
    input->mouse_wheel = (int16_t)(status[12] >> 16);
    for (i = 0; i < 16; i++) {
        input->keys[i] = status[16 + i];
    }

    // The OSD menu is open: Main_MiSTer passes the core no buttons and keys
    // then, and nothing else reaches the game either, nor what was held when
    // it closed (the button that closed it), until it is let go
    if (open) {
        mouse_off[0] += input->mouse_x - last_mouse[0];
        mouse_off[1] += input->mouse_y - last_mouse[1];
        wheel_off += input->mouse_wheel - last_wheel;
    }
    last_mouse[0] = input->mouse_x;
    last_mouse[1] = input->mouse_y;
    last_wheel = input->mouse_wheel;
    input->mouse_x -= mouse_off[0];
    input->mouse_y -= mouse_off[1];
    input->mouse_wheel -= wheel_off;
    held_mouse = open ? 7 : held_mouse & input->mouse_buttons;
    input->mouse_buttons &= ~held_mouse;
    for (i = 0; i < 16; i++) {
        held_keys[i] = open ? ~0u : held_keys[i] & input->keys[i];
        input->keys[i] &= ~held_keys[i];
    }
    for (i = 0; i < 2; i++) {
        held_joy[i] = open ? ~0u : held_joy[i] & input->joystick[i];
        input->joystick[i] &= ~held_joy[i];
        axes[0] = &input->analog_lx[i];
        axes[1] = &input->analog_ly[i];
        axes[2] = &input->analog_rx[i];
        axes[3] = &input->analog_ry[i];
        for (k = 0; k < 4; k++) {
            if (open || (held_axis[i][k] && *axes[k] > -STICK_REST && *axes[k] < STICK_REST)) {
                held_axis[i][k] = open;
            }
            if (held_axis[i][k]) {
                *axes[k] = 0;
            }
        }
    }
}

int MH_OSDTriggers(void) {
    static int last = -1;
    int now, changed;

    if (!MH_IsOpen() || status[3] < 13) {
        return 0;
    }
    now = (int)(status[9] >> 24) & 15;   // OSD status bits 56..59
    changed = last < 0 ? 0 : (now ^ last);
    last = now;
    return changed;
}

uint64_t MH_OSDStatusHigh(void) {
    // status[34..35]: the word at 0xC8, written by host version 13 and later
    if (!MH_IsOpen() || status[3] < 13) {
        return 0;
    }
    return status[34] | ((uint64_t)status[35] << 32);
}

// The option's entries start at the default: entry n is (n + 10) % 25 pixels
static int hud_margin(uint64_t high, int lsb) {
    int entry = (int)((high >> (lsb - 64)) & 31);
    return entry > MH_HUD_MAX ? MH_HUD_DEFAULT : (entry + MH_HUD_DEFAULT) % (MH_HUD_MAX + 1);
}

void MH_HudMargins(int* top, int* bottom) {
    uint64_t high = MH_OSDStatusHigh();
    const char* env = getenv("MISTER_HYBRID_HUD");
    int t, b;

    // "top,bottom" fixes them, for a test or a screenshot
    if (env != NULL && sscanf(env, "%d,%d", &t, &b) == 2) {
        *top = t < 0 ? 0 : t > MH_HUD_MAX ? MH_HUD_MAX : t;
        *bottom = b < 0 ? 0 : b > MH_HUD_MAX ? MH_HUD_MAX : b;
        return;
    }
    *top = hud_margin(high, 77);
    *bottom = hud_margin(high, 82);
}

uint64_t MH_OSDStatus(void) {
    if (!MH_IsOpen()) {
        return 0;
    }
    return status[8] | ((uint64_t)status[9] << 32);
}

void MH_AudioEnable(int enabled) {
    volatile uint32_t* ring;
    int i;

    if (!MH_IsOpen() || enabled == audio_enabled) {
        return;
    }
    if (enabled) {
        // the core starts fetching at frame 0 of the ring at the next vblank
        ring = (volatile uint32_t*)(shm + AUDIO_RING_OFFSET);
        for (i = 0; i < MH_AUDIO_RING_FRAMES; i++) {
            ring[i] = 0;
        }
        *(volatile uint32_t*)(shm + AUDIO_PTR_OFFSET) = 0;
        audio_wr = 0;
    }
    if (!enabled) {
        // the chip goes with the audio
        fm_enabled = 0;
        fm_wr = 0;
    }
    audio_enabled = enabled;
    write_ctrl();
}

// MISTER_HYBRID_STATS=1 in the environment: every 10 seconds of sound a line on
// stderr that tells how close the audio ring came to running empty. `ahead` is
// what the core had left to play when the next block arrived.
static void audio_stats(int32_t ahead, int count) {
    static int enabled = -1;
    static int32_t min_ahead = 0x7fffffff;
    static unsigned writes, underruns, frames;

    if (enabled < 0) {
        enabled = getenv("MISTER_HYBRID_STATS") != NULL;
    }
    if (!enabled) {
        return;
    }
    if (writes++ > 0) {
        if (ahead < min_ahead) {
            min_ahead = ahead;
        }
        if (ahead <= 0) {
            underruns++;
        }
    }
    frames += count;
    if (frames >= 10 * MH_AUDIO_RATE) {
        fprintf(stderr, "mister: audio, %u blocks of %d frames, least left in the ring %d frames, %u times empty\n", writes,
                count, (int)min_ahead, underruns);
        min_ahead = 0x7fffffff;
        writes = 1;
        underruns = 0;
        frames = 0;
    }
}

void MH_AudioWrite(const uint32_t* frames, int count, int lead) {
    volatile uint32_t* ring;
    volatile uint32_t* fetch_ptr;
    int32_t ahead;
    int i;

    if (!MH_IsOpen() || !audio_enabled) {
        // keep the caller's pace without the core
        struct timespec ts = { 0, (long)count * 1000000000LL / MH_AUDIO_RATE };
        nanosleep(&ts, NULL);
        return;
    }
    ring = (volatile uint32_t*)(shm + AUDIO_RING_OFFSET);
    fetch_ptr = (volatile uint32_t*)(shm + AUDIO_PTR_OFFSET);

    audio_stats((int32_t)(audio_wr - *fetch_ptr), count);

    // the core's sample clock paces us
    for (i = 0; i < 1000 && MH_IsOpen(); i++) {
        ahead = (int32_t)(audio_wr - *fetch_ptr);
        if (ahead <= lead) {
            break;
        }
        sleep_ms(1);
    }
    if (!MH_IsOpen()) {
        return;
    }
    ahead = (int32_t)(audio_wr - *fetch_ptr);
    if (ahead < 0 || ahead > MH_AUDIO_RING_FRAMES - count) {
        // fell behind (the core repeats its last sample meanwhile): skip ahead
        audio_wr = *fetch_ptr + 64;
    }
    for (i = 0; i < count; i++) {
        ring[(audio_wr + i) % MH_AUDIO_RING_FRAMES] = frames[i];
    }
    audio_wr += count;
}

int MH_FMAvailable(void) {
    return MH_IsOpen() && status[3] >= 10;
}

void MH_FMVolume(int volume) {
    fm_volume = volume < 0 ? 0 : volume > 127 ? 127 : volume;
    if (MH_IsOpen()) {
        write_ctrl();
    }
}

void MH_FMWrite(int reg, int value, int ahead) {
    volatile uint32_t* ring;
    uint32_t at;

    if (!MH_IsOpen() || !fm_enabled || !audio_enabled) {
        return;
    }
    // status[15]: the number of writes the core has read
    if (fm_wr - status[15] >= MH_FM_RING_WRITES - 1) {
        return;
    }
    ring = (volatile uint32_t*)(shm + FM_RING_OFFSET);
    at = fm_wr % MH_FM_RING_WRITES;
    ring[at * 2] = audio_wr + (uint32_t)(ahead < 0 ? 0 : ahead);
    ring[at * 2 + 1] = ((uint32_t)(reg & 0xff) << 8) | (uint32_t)(value & 0xff);
    __sync_synchronize();
    // the core reads the count at its next vblank; a count is one word, so
    // this needs no lock against write_ctrl(), which writes the same number
    fm_wr++;
    ctrl[3] = fm_wr;
}

void MH_FMEnable(int enabled) {
    int reg;

    enabled = enabled != 0;
    if (!MH_FMAvailable() || !audio_enabled || enabled == fm_enabled) {
        return;
    }
    if (!enabled) {
        // the core holds the chip in reset and starts its read count from 0
        // once it has seen this, at a vblank
        fm_enabled = 0;
        write_ctrl();
        sleep_ms(40);
        fm_wr = 0;
        write_ctrl();
        return;
    }
    // The core starts its read count from 0 only when it has seen the chip
    // switched off, at a vblank. If it was on a moment ago (the audio was
    // closed and opened again inside one field) it has not: wait for that,
    // or it would take everything between its old count and the new one
    // for writes.
    fm_wr = 0;
    write_ctrl();
    for (reg = 0; reg < 100 && status[15] != 0; reg++) {
        sleep_ms(2);
    }
    fm_enabled = 1;
    write_ctrl();
    // The chip's reset does not reach the registers of its channels and
    // operators: clear them all, as an AdLib program does before it plays.
    // 0x40..0x55 are the operators' levels, where 0x3F is silence.
    for (reg = 0x01; reg <= 0xF5; reg++) {
        MH_FMWrite(reg, reg >= 0x40 && reg <= 0x55 ? 0x3F : 0, 0);
    }
}

void MH_QuitToMenu(void) {
    FILE* f;

    if (shm_is_file) {
        return;
    }
    f = fopen("/dev/MiSTer_cmd", "w");
    if (f != NULL) {
        fputs("load_core /media/fat/menu.rbf\n", f);
        fclose(f);
    }
}

// PS/2 set 2 -> USB HID usage
static const uint8_t set2_normal[0x84] = {
    [0x1C] = 0x04, [0x32] = 0x05, [0x21] = 0x06, [0x23] = 0x07, [0x24] = 0x08, [0x2B] = 0x09, // A-F
    [0x34] = 0x0A, [0x33] = 0x0B, [0x43] = 0x0C, [0x3B] = 0x0D, [0x42] = 0x0E, [0x4B] = 0x0F, // G-L
    [0x3A] = 0x10, [0x31] = 0x11, [0x44] = 0x12, [0x4D] = 0x13, [0x15] = 0x14, [0x2D] = 0x15, // M-R
    [0x1B] = 0x16, [0x2C] = 0x17, [0x3C] = 0x18, [0x2A] = 0x19, [0x1D] = 0x1A, [0x22] = 0x1B, // S-X
    [0x35] = 0x1C, [0x1A] = 0x1D,                                                             // Y, Z
    [0x16] = 0x1E, [0x1E] = 0x1F, [0x26] = 0x20, [0x25] = 0x21, [0x2E] = 0x22,                // 1-5
    [0x36] = 0x23, [0x3D] = 0x24, [0x3E] = 0x25, [0x46] = 0x26, [0x45] = 0x27,                // 6-0
    [0x5A] = 0x28, // Enter
    [0x76] = 0x29, // Escape
    [0x66] = 0x2A, // Backspace
    [0x0D] = 0x2B, // Tab
    [0x29] = 0x2C, // Space
    [0x4E] = 0x2D, // -
    [0x55] = 0x2E, // =
    [0x54] = 0x2F, // [
    [0x5B] = 0x30, // ]
    [0x5D] = 0x31, // backslash
    [0x4C] = 0x33, // ;
    [0x52] = 0x34, // '
    [0x0E] = 0x35, // `
    [0x41] = 0x36, // ,
    [0x49] = 0x37, // .
    [0x4A] = 0x38, // /
    [0x58] = 0x39, // Caps Lock
    [0x05] = 0x3A, [0x06] = 0x3B, [0x04] = 0x3C, [0x0C] = 0x3D, [0x03] = 0x3E, [0x0B] = 0x3F, // F1-F6
    [0x83] = 0x40, [0x0A] = 0x41, [0x01] = 0x42, [0x09] = 0x43, [0x78] = 0x44, [0x07] = 0x45, // F7-F12
    [0x7E] = 0x47, // Scroll Lock
    [0x77] = 0x53, // Num Lock
    [0x7C] = 0x55, // keypad *
    [0x7B] = 0x56, // keypad -
    [0x79] = 0x57, // keypad +
    [0x69] = 0x59, [0x72] = 0x5A, [0x7A] = 0x5B, [0x6B] = 0x5C, [0x73] = 0x5D, // keypad 1-5
    [0x74] = 0x5E, [0x6C] = 0x5F, [0x75] = 0x60, [0x7D] = 0x61, [0x70] = 0x62, // keypad 6-0
    [0x71] = 0x63, // keypad .
    [0x61] = 0x64, // non-US backslash
    [0x14] = 0xE0, // left Ctrl
    [0x12] = 0xE1, // left Shift
    [0x11] = 0xE2, // left Alt
    [0x59] = 0xE5, // right Shift
};

// E0-prefixed PS/2 set 2 codes
static const uint8_t set2_extended[0x80] = {
    [0x7C] = 0x46, // Print Screen
    [0x70] = 0x49, // Insert
    [0x6C] = 0x4A, // Home
    [0x7D] = 0x4B, // Page Up
    [0x71] = 0x4C, // Delete
    [0x69] = 0x4D, // End
    [0x7A] = 0x4E, // Page Down
    [0x74] = 0x4F, // Right
    [0x6B] = 0x50, // Left
    [0x72] = 0x51, // Down
    [0x75] = 0x52, // Up
    [0x4A] = 0x54, // keypad /
    [0x5A] = 0x58, // keypad Enter
    [0x2F] = 0x65, // Application
    [0x1F] = 0xE3, // left GUI
    [0x14] = 0xE4, // right Ctrl
    [0x11] = 0xE6, // right Alt
    [0x27] = 0xE7, // right GUI
};

int MH_KeyToHID(int key_index) {
    int code = key_index & 0xff;

    if (key_index & 0x100) {
        return code < 0x80 ? set2_extended[code] : 0;
    }
    return code < 0x84 ? set2_normal[code] : 0;
}
