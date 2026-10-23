// SPDX-License-Identifier: GPL-2.0-or-later
// Part of Hybrid_MiSTer (hps/LICENSE).
//
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
#include "mister_diag.h"
#include "mister_font.h"
#include "mister_midi.h"

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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
#define STATUS_MT32 (1u << 23)
#define STATUS_SYNTH (1u << 28) // the MIDI synthesizer is in the core (host version 16)
#define STATUS_OVL (1u << 29)   // the overlay's handover has begun (host version 17)
#define STATUS_OVL_OUT (1u << 30) // the overlay is out, as w2[58] asked (host version 17)
#define CTRL_OVL (1u << 26)     // w0[58] in ctrl[1]: the overlay (host version 17)
#define CTRL_OVL_OUT (1u << 26) // w2[58] in ctrl[5]: the overlay goes out, and stays out
#define CTRL_OVL_RISE (1u << 27) // w2[59] in ctrl[5]: it rises in and sinks out (the logo)

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

// The game's logo at its start (host version 17, docs/game_logo.md): once
// the core's intro is over, "Checking Game Data..." and "Game Data OK" under
// the MiSTer logo, which then goes with them; "Loading <title>..." while the
// screen is empty, and the game's logo drawn into a framebuffer the core shows
// over its picture (the overlay), where the MiSTer logo was. The game's first
// frame is held back until the logo has been there 2 seconds.
#define LOGO_BOTTOM 295 // the MiSTer logo's bottom edge, the game's logo's too
#define LOGO_INTRO 300  // the field counter once the core's intro is over (it counts from the core's load; CrispyDoom on the MiSTer, 2026-10-22: the handover began at 298 to 310)
#define LOGO_GAP 32     // fields from the handover's start to the screen empty (the MiSTer logo gone at OVL_T_GAP, 31)
#define LOGO_IN 92      // fields from the handover's start to the logo fully in (OVL_T_IN + OVL_D_IN)
#define LOGO_LINE 24    // fields a line is shown before the next comes (0.4 seconds)
#define LOGO_HOLD 120   // fields the logo stays once it is in (2 seconds)
#define LOGO_WAIT 900   // fields to wait for the handover before going on without it (15 seconds)
#define LOGO_LINES 3
static const char* const logo_text[LOGO_LINES] = { "Checking Game Data...", "Game Data OK", "Loading %s..." };
static int overlay_mode; // MH_Overlay(1): the program's frames go on the overlay
static struct {
    int on;             // the game's logo is coming: the game's frames wait
    int asked;          // the overlay is asked for (w0[58]); before, the lines under the MiSTer logo
    int out;            // the logo going out for the program's page (overlay_mode)
    int first;          // the first of the lines shown: LOGO_LINES - 1, "Loading" alone
    uint32_t t0;        // the field "Checking Game Data..." was shown (0: not yet)
    char id[32];
    int leaving;        // given up, the picture going out with it: still 8bpp
    int in_known;       // the handover was seen, `in` is the field the logo is fully in
    uint32_t in;
    uint32_t start;     // the field it was asked for
    int lines;          // the lines shown so far
    int stop;           // the thread is to end
    volatile int done;  // it has
    int taken;          // another message took the lines' place
    char title[64];
    pthread_t thread;
    int thread_on;
    uint32_t palette[256]; // the game's palette, held back until its first frame
    int palette_held;
} logo;
static pthread_mutex_t logo_lock = PTHREAD_MUTEX_INITIALIZER;
static void logo_release(void);
static void logo_start_thread(void);
static void logo_end_thread(void);

static int cpu1_allowed;
static pid_t game_tid; // the thread that called MH_Open()
// the CPUs the game thread had before MH_Open() pinned it to CPU0, given back
// by MH_Close(): a program started in its place (execv) inherits them
// (CrispyDoom's Heretic, Hexen and Strife ran with CPU0 alone, everything the
// helper threads do on the game thread; 2026-10-21)
static cpu_set_t open_cpus;
static int open_cpus_saved;

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

// The MT32-pi's cable, for MH_MidiBacklog(): the core sends a message's bytes
// once the audio has reached its frame, at 31250 baud (10 bits a byte, 320us:
// 14.112 audio frames), so a burst (a reset, a song's settings sent again)
// holds back what comes after it. pi_free is the audio frame at which the
// last byte put in the ring will have gone out, pi_free_frac its thousandths.
#define PI_BYTE_MILLIFRAMES 14112
static uint32_t pi_free, pi_free_frac;

// FM chip: ring of register writes at FM_RING_OFFSET, 64 bits each
// {16'b0, reg, value, audio frame}. Since host version 15 it also carries
// MIDI messages for the MT32-pi: {6'b0, n, byte 2, byte 1, byte 0, audio
// frame}, n = 1..3 bytes. It is read while the chip or MIDI is enabled.
#define FM_RING_OFFSET 0x20000
static int fm_enabled;
static int fm_volume = 32;
static uint32_t fm_wr;
// the ring's writers: the audio thread and, with ahead 0, any other
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;

// MIDI music: to an MT32-pi on the user port (host version 15), its
// messages in the FM ring, or to the synthesizer in the core (host version
// 16, docs/midi_synth.md): commands of 16 words of 64 bits in a ring at
// MIDI_RING_OFFSET; the SoundFont's samples in a region of their own, mapped
// only while the synthesizer is used
#define MIDI_RING_OFFSET 0x30000
#define MIDI_SAMPLES_PHYS 0x31000000
#define MIDI_SAMPLES_FILE 0x01000000 // in fakecore's file
#define MIDI_SAMPLES_SIZE (MH_MIDI_MAX_SAMPLES * 2)
static int midi_enabled;
static int midi_volume = 32;
static int midi_program_dest = MH_MIDI_OSD;  // MH_MidiDest(): where the music goes, the program's choice
static int midi_fade = 32; // 32 down to 0 while a SoundFont is changed
static int info_n, info_seq; // MH_OSDInfo(): the text, and a count of the requests
static uint32_t midi_wr;

static void midi_off(void);
static void midi_close(void);
static void midi_service(void);

static void write_ctrl(void) {
    int ovl = logo.on && logo.asked;

    // Palette/framebuffer writes must land before the FPGA can see the new control block
    pthread_mutex_lock(&ctrl_lock);
    __sync_synchronize();
    ctrl[2] = palette_seq;
    ctrl[3] = fm_wr;
    ctrl[4] = midi_wr;
    ctrl[5] = (uint32_t)(midi_volume * midi_fade / 32) | ((uint32_t)(midi_enabled && audio_enabled) << 7)
        | ((uint32_t)info_n << 8) | ((uint32_t)(info_seq & 0xff) << 16)
        | ((uint32_t)(midi_program_dest != MH_MIDI_OSD) << 24) | ((uint32_t)(midi_program_dest == MH_MIDI_PI) << 25)
        | (logo.out ? CTRL_OVL_OUT : 0) | (ovl ? CTRL_OVL_RISE : 0);
    // the overlay is 640x480 at 8bpp; given up, the picture goes out with
    // it in the game's mode, still read at 8bpp. The logo rises into place
    // (ctrl[5] above), and goes out for a program's own page.
    ctrl[1] = fb_current
        | ((ovl || logo.leaving || overlay_mode ? MH_FORMAT_INDEX8 : fb_format) << 8)
        | ((ovl || overlay_mode ? MH_MODE_640x480 : fb_mode) << 12) | (palette_slot << 16) | (audio_enabled << 24)
        | ((uint32_t)fm_volume << 17) | ((uint32_t)(fm_enabled && audio_enabled) << 25)
        | (ovl || overlay_mode ? CTRL_OVL : 0) | ((uint32_t)menu_mask << 28);
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
    midi_enabled = 0;
    fb_mode = MH_MODE_320x200;
    // The core reloads the palette when its sequence number changes, and it
    // may know the number an earlier program ended with: count on from there
    menu_mask = 0;
    if (ctrl[0] == CTRL_MAGIC) {
        palette_seq = ctrl[2];
        palette_slot = (ctrl[1] >> 16) & 1;
    }
    // a program started in the place of the one that showed the game's logo
    // (execv: CrispyDoom's Heretic, Hexen and Strife) takes the logo over, as
    // MH_Close() left it: the lines under the MiSTer logo, or the overlay
    env = getenv("MISTER_HYBRID_LOGO");
    memset(&logo, 0, sizeof(logo));
    if (env != NULL && status[3] >= 17
        && sscanf(env, "%d %d %u %u %u %d %d %31s %63[^\n]", &logo.asked, &logo.in_known, &logo.in, &logo.start,
               &logo.t0, &logo.first, &logo.lines, logo.id, logo.title) == 9
        && (!logo.asked || (ctrl[0] == CTRL_MAGIC && (ctrl[1] & CTRL_OVL)))) {
        logo.on = 1;
        if (logo.asked) {
            ctrl_enabled = 1;
            fb_current = (int)(ctrl[1] & 0xff) % FB_COUNT;
        }
    } else {
        memset(&logo, 0, sizeof(logo));
    }
    unsetenv("MISTER_HYBRID_LOGO");
    alive_time.tv_sec = 0;
    for (i = 0; i < FB_COUNT; i++) {
        // all of it: whatever ran before us may have used a larger mode
        if (!logo.asked || i != fb_current) {
            clear_framebuffer(i, FB_SIZE);
        }
    }
    write_ctrl();

    // With both CPUs allowed (taskset 0x03 in the launcher) the game thread
    // gets CPU0 to itself: it has the better DDR3 bandwidth and Main_MiSTer
    // lives on CPU1. Helper threads go to CPU1 (MH_PinThreadToCPU1).
    cpu1_allowed = 0;
    if (sched_getaffinity(0, sizeof(cpus), &cpus) == 0 && CPU_ISSET(0, &cpus) && CPU_ISSET(1, &cpus)) {
        cpu1_allowed = 1;
        open_cpus = cpus;
        open_cpus_saved = 1;
        CPU_ZERO(&cpus);
        CPU_SET(0, &cpus);
        sched_setaffinity(0, sizeof(cpus), &cpus);
    }
    printf("mister: %s core found (host version %u)%s\n", core_name[0] ? core_name : "hybrid", status[3],
        cpu1_allowed ? ", helper threads on CPU1" : "");
    game_tid = (pid_t)syscall(SYS_gettid);
    mh_diag_start();
    if (logo.on) {
        printf("mister: the game's logo taken over\n");
        logo_start_thread();
    }
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

// MH_Close() while the audio was on: done when the audio goes off
static int close_pending = 0;

void MH_Close(void) {
    mh_diag_stop();
    logo_end_thread();
    // the game's logo still there: it stays for a program started in this
    // one's place (execv), which takes it over (MH_Open())
    if (logo.on && shm != NULL && !detached) {
        char state[192];

        snprintf(state, sizeof(state), "%d %d %u %u %u %d %d %s %s", logo.asked, logo.in_known, logo.in, logo.start,
            logo.t0, logo.first, logo.lines, logo.id, logo.title);
        setenv("MISTER_HYBRID_LOGO", state, 1);
    }
    stop_present_thread();
    if (open_cpus_saved) {
        sched_setaffinity(0, sizeof(open_cpus), &open_cpus);
        open_cpus_saved = 0;
    }
    if (shm != NULL && audio_enabled) {
        // The audio thread may be in MH_AudioWrite(), in the memory: it is
        // unmapped once the audio is closed, MH_AudioEnable(0) (SDL closes
        // the video before the audio; a game quitting after another core was
        // loaded crashed in the audio thread, CrispyDoom 2026-10-19). The
        // core's own picture comes back now.
        if (!detached && corename_matches() && !logo.on) {
            ctrl_enabled = 0;
            write_ctrl();
        }
        close_pending = 1;
        return;
    }
    close_pending = 0;
    if (shm != NULL) {
        midi_close();
        if (!detached && corename_matches()) {
            // back to the core's own picture, audio off. Not once another
            // core is loaded: the memory is its own then. The game's logo
            // stays (above)
            ctrl_enabled = logo.on && logo.asked;
            audio_enabled = 0;
            fm_enabled = 0;
            fm_wr = 0;
            midi_enabled = 0;
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

// Text drawn into the copy of the frame that the core shows (the FPS counter,
// the SoundFont's name): line `line` of 10 pixels from 8 pixels in from the
// corner (a television hides the edge), cut at the screen's edge. With a
// palette: its lightest colour on its darkest.
static void stamp_text(volatile uint8_t* dst, int row, int height, int line, const char* text) {
    static uint32_t pal_seq = ~0u;
    static uint8_t ink8, paper8;
    int width = MH_Width();
    int bpp = row / width;
    int sx = width >= 640 ? 2 : 1;
    int sy = height >= 400 ? 2 : 1;
    int n = (int)strlen(text), c, x, y, w;

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
    if (n > (width / sx - 18) / 8) {
        n = (width / sx - 18) / 8;
    }
    w = n * 8 + 2;
    if (n <= 0 || (18 + 10 * line) * sy > height) {
        return;
    }
    for (y = 0; y < 10 * sy; y++) {
        volatile uint8_t* out = dst + ((8 + 10 * line) * sy + y) * row;

        for (x = 0; x < w * sx; x++) {
            int gx = x / sx - 1, gy = y / sy - 1;
            int on = 0;

            if (gx >= 0 && gx < n * 8 && gy >= 0 && gy < 8) {
                unsigned char ch = (unsigned char)text[gx / 8];

                on = ch >= 32 && ch < 127 && (mh_font[ch - 32][gy] >> (gx & 7)) & 1;
            }
            if (bpp == 2) {
                ((volatile uint16_t*)out)[8 * sx + x] = on ? 0xFFE0 : 0x0000;
            } else {
                out[8 * sx + x] = on ? ink8 : paper8;
            }
        }
    }
}

// The OSD's FPS Counter (MH_OSD_FPS): the frames presented in the last second.
// Returns the lines it took.
static int fps_stamp(volatile uint8_t* dst, int row, int height) {
    static struct timespec since;
    static int count;
    static int shown = -1;
    struct timespec now;
    char text[8];
    double elapsed;
    static int forced = -2; // MISTER_HYBRID_FPS=0|1: the counter off or on whatever the OSD says (screenshots)

    if (forced == -2) {
        forced = getenv("MISTER_HYBRID_FPS") ? atoi(getenv("MISTER_HYBRID_FPS")) != 0 : -1;
    }
    if (forced == 0 || (forced < 0 && !MH_OSD_FPS(MH_OSDStatus()))) {
        shown = -1;
        count = 0;
        return 0;
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
        return 0;
    }
    snprintf(text, sizeof text, "%d", shown > 999 ? 999 : shown);
    stamp_text(dst, row, height, 0, text);
    return 1;
}

// A note of the MIDI synthesizer (the SoundFont chosen, or why there is none),
// shown for a few seconds under the FPS counter's place
#define MIDI_NOTE_LINES 3
static pthread_mutex_t note_lock = PTHREAD_MUTEX_INITIALIZER;
static char midi_note[MIDI_NOTE_LINES][80];
static struct timespec midi_note_until;

static void midi_show(int seconds, const char* l0, const char* l1, const char* l2) {
    pthread_mutex_lock(&note_lock);
    snprintf(midi_note[0], sizeof midi_note[0], "%s", l0);
    snprintf(midi_note[1], sizeof midi_note[1], "%s", l1 ? l1 : "");
    snprintf(midi_note[2], sizeof midi_note[2], "%s", l2 ? l2 : "");
    clock_gettime(CLOCK_MONOTONIC, &midi_note_until);
    midi_note_until.tv_sec += seconds;
    pthread_mutex_unlock(&note_lock);
    fprintf(stderr, "mister: %s%s%s%s%s\n", l0, l1 ? " " : "", l1 ? l1 : "", l2 ? " " : "", l2 ? l2 : "");
}

static void midi_stamp(volatile uint8_t* dst, int row, int height, int line) {
    struct timespec now;
    int k;

    if (midi_note[0][0] == 0) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    pthread_mutex_lock(&note_lock);
    if (now.tv_sec > midi_note_until.tv_sec
        || (now.tv_sec == midi_note_until.tv_sec && now.tv_nsec >= midi_note_until.tv_nsec)) {
        midi_note[0][0] = 0;
    }
    for (k = 0; k < MIDI_NOTE_LINES && midi_note[0][0] != 0 && midi_note[k][0] != 0; k++) {
        stamp_text(dst, row, height, line + k, midi_note[k]);
    }
    pthread_mutex_unlock(&note_lock);
}

// MISTER_HYBRID_PACE=1: every 300 frames, on stderr, how the frames came to
// the core. It shows the newest at each vblank, so a frame handed over in the
// same field as the one before means that one was never shown, and one two
// fields after the one before (where one was meant) that the frame before was
// shown twice: the stutter of a game that is at 60 frames per second by its
// own count. Also how long after the start of a field (as the game saw it in
// MH_WaitFrame() or MH_FieldCounter()) frames were handed over, at most.
// The diagnostic report (mister_diag.c) counts the same all the time.
static int pace_on = -1;
static uint32_t pace_field_seen;
static struct timespec pace_field_time;

// The diagnostic report's counts since mh_diag_take(): the present thread
// (or the game thread) and the audio thread add to them
static struct {
    uint32_t frames, never, twice, late_us, underruns;
    int ahead;
} diag;

static void pace_field(uint32_t field) {
    if (field != pace_field_seen) {
        pace_field_seen = field;
        clock_gettime(CLOCK_MONOTONIC, &pace_field_time);
    }
}

static void pace_flip(uint32_t field) {
    static uint32_t last, diag_last;
    static int diag_started;
    static unsigned frames, same, next, later;
    static double latest;
    // interlaced, the core takes a frame every second field
    uint32_t step = (status[2] & STATUS_LACE) ? 2 : 1;
    uint32_t late_us = 0;
    struct timespec now;

    if (field == pace_field_seen) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        late_us = (uint32_t)((now.tv_sec - pace_field_time.tv_sec) * 1000000
                             + (now.tv_nsec - pace_field_time.tv_nsec) / 1000);
    }
    __atomic_fetch_add(&diag.frames, 1, __ATOMIC_RELAXED);
    if (diag_started && field - diag_last < step) {
        __atomic_fetch_add(&diag.never, 1, __ATOMIC_RELAXED);
    } else if (diag_started && field - diag_last > step) {
        __atomic_fetch_add(&diag.twice, 1, __ATOMIC_RELAXED);
    }
    diag_started = 1;
    diag_last = field;
    if (late_us > __atomic_load_n(&diag.late_us, __ATOMIC_RELAXED)) {
        __atomic_store_n(&diag.late_us, late_us, __ATOMIC_RELAXED);
    }

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
    if (field == pace_field_seen && late_us * 1e-3 > latest) {
        latest = late_us * 1e-3;
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
    midi_stamp(dst, row, height, fps_stamp(dst, row, height));
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
    // the program's page after the logo: in again with its frame
    logo.out = 0;
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
    // on the overlay the core's picture stays: nothing to wait for (host
    // version 17); a message under the MiSTer logo goes out with it
    if (overlay_mode) {
        screen_ours = 1;
        MH_ClearMessage();
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
// A message of the game's own takes the place of the logo's lines: they stop
static void logo_taken(void) {
    if (logo.thread_on && !pthread_equal(pthread_self(), logo.thread)) {
        pthread_mutex_lock(&logo_lock);
        logo.taken = 1;
        pthread_mutex_unlock(&logo_lock);
    }
}

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
    // the core shows its picture only while no game has the screen; the
    // game's logo stays over it
    if (ctrl_enabled && !logo.on) {
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
    logo_taken();
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
    logo_taken();
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

// The game's logo (docs/game_logo.md). The .mhl file of tools/make_logo.py
// next to the program, in logos/: "MHLG", u16 width, u16 height, u16 colours
// (n), n x RGB (indexes 1..n), the pixels (0 transparent).
static int logo_load(const char* id, uint8_t* fb, uint32_t* palette) {
    char dir[512], path[600], name[80];
    unsigned char head[10];
    struct dirent* ent;
    ssize_t len;
    DIR* d;
    FILE* f;
    int w, h, n, x0, y0, y, i, ok = 0;

    // the program's folder (GAMEDIR), the file's name in any case
    len = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
    if (len <= 0 || id == NULL || id[0] == 0) {
        return 0;
    }
    dir[len] = 0;
    *strrchr(dir, '/') = 0;
    snprintf(path, sizeof(path), "%s/logos", dir);
    snprintf(name, sizeof(name), "%s.mhl", id);
    d = opendir(path);
    if (d == NULL) {
        return 0;
    }
    while ((ent = readdir(d)) != NULL && strcasecmp(ent->d_name, name) != 0) {
    }
    if (ent != NULL) {
        snprintf(path + strlen(path), sizeof(path) - strlen(path), "/%s", ent->d_name);
    }
    closedir(d);
    if (ent == NULL || (f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    if (fread(head, 1, 10, f) == 10 && memcmp(head, "MHLG", 4) == 0) {
        w = head[4] | head[5] << 8;
        h = head[6] | head[7] << 8;
        n = head[8] | head[9] << 8;
        x0 = (640 - w) / 2;
        y0 = LOGO_BOTTOM - h;
        if (w > 0 && w <= 640 && h > 0 && y0 >= 0 && n >= 1 && n <= 255) {
            unsigned char rgb[255 * 3];
            ok = fread(rgb, 3, n, f) == (size_t)n;
            memset(palette, 0, 256 * sizeof(*palette));
            for (i = 0; ok && i < n; i++) {
                palette[i + 1] = (uint32_t)rgb[3 * i] << 16 | rgb[3 * i + 1] << 8 | rgb[3 * i + 2];
            }
            memset(fb, 0, 640 * 480);
            for (y = 0; ok && y < h; y++) {
                ok = fread(fb + (y0 + y) * 640 + x0, 1, w, f) == (size_t)w;
            }
        }
    }
    fclose(f);
    if (!ok) {
        fprintf(stderr, "mister: %s: not a logo of make_logo.py\n", path);
    }
    return ok;
}

// The lines `from` up to `to` of logo_text, from the message's first row
static void logo_lines(int from, int to) {
    char text[(MH_MSG_COLS + 1) * LOGO_LINES];
    size_t n = 0;
    int i;

    for (i = from; i < to && i < LOGO_LINES; i++) {
        n += snprintf(text + n, sizeof(text) - n, logo_text[i], logo.title);
        n += snprintf(text + n, sizeof(text) - n, "\n");
    }
    pthread_mutex_lock(&logo_lock);
    if (!logo.taken) {
        MH_ShowMessage(text, 0);
        logo.lines = to;
    }
    pthread_mutex_unlock(&logo_lock);
}

// Wait for field `at`. 0 when the core is gone or the thread is to end.
static int logo_until(uint32_t at) {
    while (MH_IsOpen() && !logo.stop) {
        if ((int32_t)(status[1] - at) >= 0) {
            return 1;
        }
        sleep_ms(5);
    }
    return 0;
}

// Wait for the handover to be seen, then for `after_in` fields from the logo
// fully in. 0 when the logo is to go on without it: the core is gone, the
// thread is to end, or no handover came.
static int logo_wait(int after_in) {
    for (;;) {
        uint32_t now;

        if (!MH_IsOpen() || logo.stop) {
            return 0;
        }
        now = status[1];
        if (!logo.in_known) {
            if (status[2] & STATUS_OVL) {
                // the handover has begun: the logo is in LOGO_IN fields. The
                // message there (the lines, the list) goes out with the
                // MiSTer logo
                logo.in = now + LOGO_IN;
                logo.in_known = 1;
                if (logo.lines < LOGO_LINES) {
                    MH_ClearMessage();
                }
            } else if ((int32_t)(now - (logo.start + LOGO_WAIT)) >= 0) {
                return 0;
            }
        }
        if (logo.in_known && (int32_t)(now - (logo.in + after_in)) >= 0) {
            return 1;
        }
        sleep_ms(5);
    }
}

// Ask for the overlay with the game's logo: 0 without its file
static int logo_ask(void) {
    uint32_t palette[256];
    volatile uint32_t* pal;
    uint8_t* image;
    int fb, i;

    if ((image = malloc(640 * 480)) == NULL || !logo_load(logo.id, image, palette)) {
        free(image);
        return 0;
    }
    present_flush();
    // into a framebuffer the core does not show, and a palette slot
    fb = (fb_current + 1) % FB_COUNT;
    memcpy((void*)framebuffer(fb), image, 640 * 480);
    free(image);
    palette_slot ^= 1;
    pal = (volatile uint32_t*)(shm + PALETTE_OFFSET + palette_slot * PALETTE_SLOT_SIZE);
    for (i = 0; i < 256; i++) {
        pal[i] = palette[i];
    }
    palette_seq++;
    fb_current = fb;
    logo.start = status[1];
    logo.asked = 1;
    ctrl_enabled = 1;
    write_ctrl();
    printf("mister: the logo of %s\n", logo.id);
    return 1;
}

// "Loading <title>..." alone under the MiSTer logo: the game goes on without
// its logo
static void logo_loading_alone(void) {
    char text[96];

    pthread_mutex_lock(&logo_lock);
    if (!logo.taken) {
        snprintf(text, sizeof(text), logo_text[LOGO_LINES - 1], logo.title);
        MH_ShowMessage(text, 0);
    }
    pthread_mutex_unlock(&logo_lock);
}

// Once the intro is over the first lines, each LOGO_LINE fields before the
// next, under the MiSTer logo; then the handover, the MiSTer logo going out
// with them, and "Loading" in the first row while the screen is empty
static void* logo_main(void* arg) {
    (void)arg;
    MH_PinThreadToCPU1();
    if (!logo.asked) {
        if (logo.first == 0 && logo.t0 == 0) {
            if (!logo_until(LOGO_INTRO)) {
                goto done;
            }
            logo.t0 = status[1];
            logo_lines(0, 1);
        }
        if (logo.first == 0 && logo.lines < 2) {
            if (!logo_until(logo.t0 + LOGO_LINE)) {
                goto done;
            }
            logo_lines(0, 2);
        }
        if (logo.first == 0 && !logo_until(logo.t0 + 2 * LOGO_LINE)) {
            goto done;
        }
        // a message of the game's own meanwhile (an error): no logo over it
        if (logo.taken) {
            goto done;
        }
        if (!logo_ask()) {
            logo_loading_alone();
            goto done;
        }
    }
    if (logo.lines < LOGO_LINES) {
        if (!logo_wait(LOGO_GAP - LOGO_IN)) {
            // no handover: the last line alone, under the MiSTer logo
            if (!logo.stop && MH_IsOpen() && !logo.in_known) {
                logo_loading_alone();
            }
            goto done;
        }
        logo_lines(LOGO_LINES - 1, LOGO_LINES);
    }
done:
    logo.done = 1;
    return NULL;
}

static void logo_start_thread(void) {
    logo.stop = 0;
    logo.done = 0;
    logo.taken = 0;
    logo.thread_on = pthread_create(&logo.thread, NULL, logo_main, NULL) == 0;
}

static void logo_end_thread(void) {
    if (logo.thread_on) {
        logo.stop = 1;
        pthread_join(logo.thread, NULL);
        logo.thread_on = 0;
    }
}

// The game's logo is to come: 1 when it is there (host version 17, its file).
// The thread shows the lines and asks for the overlay.
static int logo_show(const char* id, const char* title, int first) {
    uint32_t palette[256];
    uint8_t* image;
    int ok;

    if (!MH_IsOpen() || status[3] < 17 || logo.on || screen_ours || (image = malloc(640 * 480)) == NULL) {
        return 0;
    }
    ok = logo_load(id, image, palette);
    free(image);
    if (!ok) {
        return 0;
    }
    memset(&logo, 0, sizeof(logo));
    snprintf(logo.id, sizeof(logo.id), "%s", id);
    snprintf(logo.title, sizeof(logo.title), "%s", title);
    logo.first = first;
    logo.on = 1;
    return 1;
}

static int game_logo(const char* id, const char* title, int first) {
    char text[MH_MSG_COLS + 1];

    if (logo_show(id, title, first)) {
        logo_start_thread();
        return 1;
    }
    // no logo: the last line alone, under the MiSTer logo
    snprintf(text, sizeof(text), logo_text[LOGO_LINES - 1], title);
    MH_ShowMessage(text, 0);
    return 0;
}

int MH_GameLogo(const char* id, const char* title) {
    return game_logo(id, title, 0);
}

int MH_GameLogoLoading(const char* id, const char* title) {
    return game_logo(id, title, LOGO_LINES - 1);
}

int MH_Overlay(int on) {
    if (!MH_IsOpen() || status[3] < 17) {
        return 0;
    }
    overlay_mode = on != 0;
    write_ctrl();
    return 1;
}

// The game's files are not there: no logo, "Checking Game Data..." under the
// MiSTer logo once its intro is over, before the message naming them
void MH_GameLogoMissing(const char* id) {
    uint32_t at;

    (void)id;
    if (!MH_IsOpen() || status[3] < 17) {
        return;
    }
    while (MH_IsOpen() && (int32_t)(status[1] - LOGO_INTRO) < 0) {
        sleep_ms(5);
    }
    at = status[1];
    MH_ShowMessage(logo_text[0], 0);
    while (MH_IsOpen() && (int32_t)(status[1] - (at + LOGO_LINE)) < 0) {
        sleep_ms(5);
    }
}

// The game's first frame: the logo's 2 seconds are over, then it is given up
// and goes out with the core's picture, and the game gets the screen (black
// until its frame is there)
static void logo_release(void) {
    int i;

    if (!logo.on) {
        return;
    }
    // the lines first (the thread sees the handover), then the 2 seconds
    while (logo.thread_on && !logo.done && MH_IsOpen()) {
        sleep_ms(5);
    }
    if (logo.in_known) {
        logo_wait(LOGO_HOLD);
    }
    logo_end_thread();
    if (!MH_IsOpen()) {
        logo.on = 0;
        return;
    }
    // it never came (no file, a message of the game's own): the game as
    // without it
    if (!logo.asked) {
        logo.on = 0;
        write_ctrl();
        if (logo.palette_held) {
            logo.palette_held = 0;
            MH_SetPalette(logo.palette);
        }
        return;
    }
    if (overlay_mode) {
        // the program's page in the logo's place: the logo goes out (as it
        // came, backwards), the line with it; out of sight the page's palette
        // goes in, and its frame (present_to_fpga()) brings the overlay back
        logo.out = 1;
        write_ctrl();
        for (i = 0; i < 300 && MH_IsOpen() && !(status[2] & STATUS_OVL_OUT); i++) {
            sleep_ms(10);
        }
        MH_ClearMessage();
        for (i = 0; i < 50 && MH_IsOpen() && (status[2] & STATUS_MSG); i++) {
            sleep_ms(10);
        }
        logo.on = 0;
        if (logo.palette_held) {
            volatile uint32_t* pal;

            logo.palette_held = 0;
            palette_slot ^= 1;
            pal = (volatile uint32_t*)(shm + PALETTE_OFFSET + palette_slot * PALETTE_SLOT_SIZE);
            for (i = 0; i < 256; i++) {
                pal[i] = logo.palette[i] & 0xffffff;
            }
            palette_seq++;
        }
        return;
    }
    logo.on = 0;
    logo.leaving = 1;
    write_ctrl();
    for (i = 0; i < 300 && MH_IsOpen() && !(status[2] & STATUS_GAME); i++) {
        sleep_ms(10);
    }
    logo.leaving = 0;
    write_ctrl();
    if (logo.palette_held) {
        logo.palette_held = 0;
        MH_SetPalette(logo.palette);
    }
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
    // the game's logo is in a framebuffer: the others are black still
    if (logo.on) {
        fb_format = format;
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
    if (logo.on) {
        fb_mode = mode;
        return 1;
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
    logo_release();
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
    logo_release();
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
    // the game's logo is on the screen: its palette waits for the first frame
    if (logo.on) {
        memcpy(logo.palette, rgb, sizeof(logo.palette));
        logo.palette_held = 1;
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
    if (ahead) {
        diag.ahead = 1;
    }
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

void MH_OSDInfo(int n) {
    if (!MH_IsOpen() || n < 1 || n > 255) {
        return;
    }
    pthread_mutex_lock(&ctrl_lock);
    info_n = n;
    info_seq++;
    pthread_mutex_unlock(&ctrl_lock);
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

// The option's entry n is n pixels (host version 16; 0 the default). Up to
// host version 15 the entries started at 10, the default then: entry n was
// (n + 10) % 25 pixels
static int hud_margin(uint64_t high, int lsb) {
    int entry = (int)((high >> (lsb - 64)) & 31);

    if (!MH_IsOpen() || status[3] < 13 || entry > MH_HUD_MAX) {
        return MH_HUD_DEFAULT;
    }
    return status[3] >= 16 ? entry : (entry + 10) % (MH_HUD_MAX + 1);
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

    if (!enabled && close_pending) {
        // the audio thread is done: MH_Close() can unmap the memory
        audio_enabled = 0;
        fm_enabled = 0;
        fm_wr = 0;
        midi_enabled = 0;
        MH_Close();
        return;
    }
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
        pi_free = 0;
        pi_free_frac = 0;
    }
    if (!enabled) {
        // the chip and MIDI go with the audio (the core tells the MT32-pi
        // to stop its notes)
        fm_enabled = 0;
        fm_wr = 0;
        midi_off();
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
        __atomic_fetch_add(&diag.underruns, 1, __ATOMIC_RELAXED);
    }
    for (i = 0; i < count; i++) {
        ring[(audio_wr + i) % MH_AUDIO_RING_FRAMES] = frames[i];
    }
    audio_wr += count;
    midi_service();
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

// An entry into the FM ring, made at audio frame `ahead` of the next block;
// 0 if the ring is full
static int ring_put(int ahead, uint32_t entry) {
    volatile uint32_t* ring;
    uint32_t at;

    pthread_mutex_lock(&ring_lock);
    // status[15]: the number of entries the core has read
    if (fm_wr - status[15] >= MH_FM_RING_WRITES - 1) {
        pthread_mutex_unlock(&ring_lock);
        return 0;
    }
    ring = (volatile uint32_t*)(shm + FM_RING_OFFSET);
    at = fm_wr % MH_FM_RING_WRITES;
    ring[at * 2] = audio_wr + (uint32_t)(ahead < 0 ? 0 : ahead);
    ring[at * 2 + 1] = entry;
    __sync_synchronize();
    // the core reads the count at its next vblank; a count is one word, so
    // this needs no lock against write_ctrl(), which writes the same number
    fm_wr++;
    ctrl[3] = fm_wr;
    pthread_mutex_unlock(&ring_lock);
    return 1;
}

// The ring is read while the chip or MIDI is enabled. The core starts its
// read count from 0 only when it has seen both off, at a vblank.
static int ring_on(void) {
    return audio_enabled && (fm_enabled || midi_enabled);
}

// before the first of the two is enabled. If the ring was on a moment ago
// (the audio was closed and opened again inside one field) the core has not
// seen it off: wait for that, or it would take everything between its old
// count and the new one for entries.
static void ring_start(void) {
    int i;

    if (ring_on()) {
        return;
    }
    fm_wr = 0;
    write_ctrl();
    for (i = 0; i < 100 && status[15] != 0; i++) {
        sleep_ms(2);
    }
}

// after one of the two was disabled
static void ring_stop(void) {
    write_ctrl();
    if (ring_on()) {
        return;
    }
    sleep_ms(40);
    fm_wr = 0;
    write_ctrl();
}

void MH_FMWrite(int reg, int value, int ahead) {
    if (!MH_IsOpen() || !fm_enabled || !audio_enabled) {
        return;
    }
    ring_put(ahead, ((uint32_t)(reg & 0xff) << 8) | (uint32_t)(value & 0xff));
}

void MH_FMEnable(int enabled) {
    int reg;

    enabled = enabled != 0;
    if (!MH_FMAvailable() || !audio_enabled || enabled == fm_enabled) {
        return;
    }
    if (!enabled) {
        // the core holds the chip in reset
        fm_enabled = 0;
        ring_stop();
        return;
    }
    ring_start();
    fm_enabled = 1;
    write_ctrl();
    // The chip's reset does not reach the registers of its channels and
    // operators: clear them all, as an AdLib program does before it plays.
    // 0x40..0x55 are the operators' levels, where 0x3F is silence.
    for (reg = 0x01; reg <= 0xF5; reg++) {
        MH_FMWrite(reg, reg >= 0x40 && reg <= 0x55 ? 0x3F : 0, 0);
    }
}

// MIDI music goes to one of two (docs/midi_synth.md, "The MT32-pi and the
// synthesizer"): with the OSD on MIDI (status bit 87), to an MT32-pi that is
// connected and on in the OSD (bit 96), else to the synthesizer in the core
// (host version 16). The synthesizer follows every channel's settings
// whichever plays, its notes only while it is the one.
enum { MIDI_NONE, MIDI_PI, MIDI_SYNTH };
#define MIDI_NAMED -2  // midi_tried: MH_MidiLoadSoundFont()'s file
static int midi_to = MIDI_NONE;          // where it went at the last block
static volatile unsigned midi_switches;  // changes of where it goes, for MH_MidiSynthChanged()

// The synthesizer's ARM side. midi_lock serialises the game's calls,
// the audio thread's service (midi_service()) and the SoundFont loader, which
// holds it only between its steps: MH_MidiEvent() never waits for a load.
static pthread_mutex_t midi_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t midi_cond = PTHREAD_COND_INITIALIZER;
static mh_midi* midi;
static mh_sfont* midi_font;
static char midi_font_path[512];
static int16_t* midi_samples;  // the sample memory, mapped
static uint32_t midi_dropped;  // commands that found the ring full
static int midi_slot;          // the SoundFont slot the OSD asks for
static int midi_tried = -1;    // the slot last loaded or tried, MIDI_NAMED for midi_named
static char midi_named[512];   // MH_MidiLoadSoundFont()'s file, "" for the OSD's slot
static unsigned midi_named_gen, midi_tried_gen;  // its requests, and the one last loaded or tried
static int midi_failed;        // the last load found no font or a bad one
static char midi_coming[512];  // the file being loaded
static int midi_ok;            // that slot's SoundFont is loaded
static int midi_loading;
static int midi_quit;
static int midi_thread_on;
static pthread_t midi_thread;

int MH_MidiAvailable(void) {
    return MH_IsOpen() && status[3] >= 15;
}

// the core has the synthesizer: host version 16, built with MIDI_SYNTH
static int midi_synth_there(void) {
    return MH_IsOpen() && status[3] >= 16 && (status[2] & STATUS_SYNTH) != 0;
}

static int midi_dest(void) {
    // status bits 64..127
    uint64_t high = MH_OSDStatusHigh();

    if (!MH_MidiAvailable() || !midi_enabled || !audio_enabled) {
        return MIDI_NONE;
    }
    if (midi_program_dest != MH_MIDI_OSD) {
        // the program's choice, not the OSD's (host version 17): the Pi
        // while one is connected, else the synthesizer
        if (midi_program_dest == MH_MIDI_PI && (status[2] & STATUS_MT32)) {
            return MIDI_PI;
        }
    } else if ((high >> 23) & 1) {
        return MIDI_NONE;
    } else if ((status[2] & STATUS_MT32) && !((high >> 32) & 1)) {
        return MIDI_PI;
    }
    return midi_synth_there() && midi != NULL ? MIDI_SYNTH : MIDI_NONE;
}

const char* MH_SoundFontDir(void) {
    static char dir[64];
    const char* env = getenv("MISTER_HYBRID_SOUNDFONTS");
    struct stat st;
    int i;

    if (env != NULL && env[0] != 0) {
        return env;
    }
    for (i = 0; i < 8; i++) {
        snprintf(dir, sizeof dir, "/media/usb%d/games/dnk/SoundFonts", i);
        if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            return dir;
        }
    }
    return "/media/fat/games/dnk/SoundFonts";
}

static int name_cmp(const void* a, const void* b) {
    return strcasecmp(*(char* const*)a, *(char* const*)b);
}

// The file slots.txt names for a slot, when it is there (docs/midiplayer.md,
// "slots.txt"): lines "<slot> <file>", # and empty lines skipped. -1 without
// slots.txt, 0 when the slot is not listed or its file is missing
static int slots_file(int slot, char* path, int size) {
    char line[320];
    FILE* f;
    int found = 0;

    snprintf(line, sizeof line, "%s/slots.txt", MH_SoundFontDir());
    f = fopen(line, "r");
    if (f == NULL) {
        return -1;
    }
    while (!found && fgets(line, sizeof line, f) != NULL) {
        size_t len = strcspn(line, "\r\n");
        struct stat st;

        line[len] = 0;
        if (line[0] >= '0' && line[0] <= '7' && line[1] == ' ' && line[2] != 0 && line[0] - '0' == slot) {
            snprintf(path, (size_t)size, "%s/%s", MH_SoundFontDir(), line + 2);
            found = stat(path, &st) == 0 && S_ISREG(st.st_mode);
        }
    }
    fclose(f);
    return found;
}

// The SoundFont of an OSD slot (docs/midi_synth.md, "Where the SoundFonts
// are"): the file slots.txt names for it; without slots.txt, 0
// GeneralUser-GS.sf2, else the first .sf2 by name, and N the Nth other .sf2
// by name, case ignored. An empty slot is slot 0's. 0 when the folder has no
// font; *own is 0 for an empty slot.
static int soundfont_of(int slot, char* path, int size, int* own) {
    DIR* d;
    struct dirent* e;
    char* names[256];
    int n = 0, def = 0, pick, i, listed;

    listed = slots_file(slot, path, size);
    if (listed == 1) {
        *own = 1;
        return 1;
    }
    if (listed == 0 && slot != 0) {
        // slots.txt has no font for it: slot 0's
        if (!soundfont_of(0, path, size, own)) {
            return 0;
        }
        *own = 0;
        return 1;
    }
    d = opendir(MH_SoundFontDir());
    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && n < 256) {
        size_t len = strlen(e->d_name);

        if (len > 4 && e->d_name[0] != '.' && strcasecmp(e->d_name + len - 4, ".sf2") == 0) {
            names[n] = strdup(e->d_name);
            n += names[n] != NULL;
        }
    }
    closedir(d);
    if (n == 0) {
        return 0;
    }
    qsort(names, (size_t)n, sizeof names[0], name_cmp);
    for (i = 0; i < n; i++) {
        if (strcasecmp(names[i], "GeneralUser-GS.sf2") == 0) {
            def = i;
        }
    }
    pick = def;
    *own = slot == 0;
    for (i = 0; i < n && slot > 0; i++) {
        if (i != def && --slot == 0) {
            pick = i;
            *own = 1;
        }
    }
    snprintf(path, (size_t)size, "%s/%s", MH_SoundFontDir(), names[pick]);
    for (i = 0; i < n; i++) {
        free(names[i]);
    }
    return 1;
}

int MH_SoundFontSlots(char files[MH_SF_SLOTS][MH_SF_NAME], int own[MH_SF_SLOTS]) {
    char path[512];
    int k, any = 0, o;

    for (k = 0; k < MH_SF_SLOTS; k++) {
        files[k][0] = 0;
        own[k] = 0;
        if (soundfont_of(k, path, sizeof path, &o)) {
            snprintf(files[k], MH_SF_NAME, "%s", strrchr(path, '/') + 1);
            own[k] = o;
            any = 1;
        }
    }
    return any;
}

int MH_SetSoundFontSlot(int slot, const char* file) {
    char files[MH_SF_SLOTS][MH_SF_NAME], path[512], tmp[520];
    int own[MH_SF_SLOTS], k, ok;
    FILE* f;

    if (slot < 0 || slot >= MH_SF_SLOTS || (file != NULL && (strchr(file, '/') != NULL || strchr(file, '\n') != NULL))) {
        return 0;
    }
    // every slot as the OSD offers it now, so that no other one moves when
    // slots.txt is written for the first time
    MH_SoundFontSlots(files, own);
    snprintf(files[slot], MH_SF_NAME, "%s", file != NULL ? file : "");
    own[slot] = file != NULL && file[0] != 0;
    snprintf(path, sizeof path, "%s/slots.txt", MH_SoundFontDir());
    snprintf(tmp, sizeof tmp, "%s.new", path);
    f = fopen(tmp, "w");
    if (f == NULL) {
        return 0;
    }
    fprintf(f, "# SoundFont slots of every core's OSD line SoundFont (docs/midiplayer.md)\n");
    for (k = 0; k < MH_SF_SLOTS; k++) {
        if (own[k] && files[k][0] != 0) {
            fprintf(f, "%d %s\n", k, files[k]);
        }
    }
    ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return 0;
    }
    return 1;
}

// A command into the ring (mh_midi's emit), under midi_lock. status[36]: the
// commands the core has read
static void midi_emit(void* ctx, const uint64_t* cmd) {
    volatile uint32_t* ring;
    int i;

    (void)ctx;
    if (!MH_IsOpen()) {
        return;
    }
    if (midi_wr - status[36] >= MH_MIDI_RING_CMDS) {
        midi_dropped++;
        return;
    }
    ring = (volatile uint32_t*)(shm + MIDI_RING_OFFSET + (midi_wr % MH_MIDI_RING_CMDS) * MH_MIDI_CMD_WORDS * 8);
    for (i = 0; i < MH_MIDI_CMD_WORDS; i++) {
        ring[2 * i] = (uint32_t)cmd[i];
        ring[2 * i + 1] = (uint32_t)(cmd[i] >> 32);
    }
    __sync_synchronize();
    // one word, as MH_FMWrite()'s count
    midi_wr++;
    ctrl[4] = midi_wr;
}

// Every voice stopped and the sample memory no longer read: the core has
// played the frame of the stop
static void midi_stop_and_wait(void) {
    uint32_t at;
    int i;

    pthread_mutex_lock(&midi_lock);
    at = audio_wr;
    if (midi != NULL) {
        mh_midi_stop_all(midi, at);
        mh_midi_set_font(midi, NULL);
        mh_midi_flush(midi);
    }
    pthread_mutex_unlock(&midi_lock);
    for (i = 0; i < 1000 && MH_IsOpen() && midi_enabled
         && (int32_t)(*(volatile uint32_t*)(shm + AUDIO_PTR_OFFSET) - at) < MH_MIDI_BLOCK; i++) {
        sleep_ms(1);
    }
}

// A load is waiting: the OSD's slot changed, or the program asked for a file
// (under midi_lock)
static int midi_pending(void) {
    if (midi_named[0] != 0) {
        return midi_tried != MIDI_NAMED || midi_tried_gen != midi_named_gen;
    }
    return midi_slot != midi_tried;
}

// Loads the slot's SoundFont, or the named file (MH_MidiLoadSoundFont(): no
// info box then, the program says what it likes), the music faded out
// meanwhile
static void midi_load(int slot, const char* named, int first) {
    char path[512], err[160], line[80];
    mh_sfont* font;
    int own = 0, k;

    if (named != NULL) {
        snprintf(path, sizeof path, "%s", named);
    } else if (!soundfont_of(slot, path, sizeof path, &own)) {
        pthread_mutex_lock(&midi_lock);
        midi_ok = 0;
        midi_failed = 1;
        pthread_mutex_unlock(&midi_lock);
        fprintf(stderr, "mister: no SoundFont in %s/, FM music instead\n", MH_SoundFontDir());
        MH_OSDInfo(MH_INFO_NO_SOUNDFONT);
        return;
    }
    pthread_mutex_lock(&midi_lock);
    snprintf(midi_coming, sizeof midi_coming, "%s", path);
    pthread_mutex_unlock(&midi_lock);
    if (!first && named == NULL) {
        if (own && slot == 0) {
            snprintf(line, sizeof line, "SoundFont Default: %s", strrchr(path, '/') + 1);
            midi_show(3, line, NULL, NULL);
        } else if (own) {
            snprintf(line, sizeof line, "SoundFont %d: %s", slot, strrchr(path, '/') + 1);
            midi_show(3, line, NULL, NULL);
        } else {
            snprintf(line, sizeof line, "SoundFont %d: none, using", slot);
            midi_show(3, line, strrchr(path, '/') + 1, NULL);
        }
    }
    if (midi_font != NULL && strcmp(path, midi_font_path) == 0) {
        pthread_mutex_lock(&midi_lock);
        midi_ok = 1;
        midi_failed = 0;
        pthread_mutex_unlock(&midi_lock);
        return;
    }
    if (midi_font != NULL) {
        // fade out over 48ms, then nothing may read the samples
        for (k = 28; k >= 0 && MH_IsOpen(); k -= 4) {
            midi_fade = k;
            write_ctrl();
            sleep_ms(6);
        }
        midi_stop_and_wait();
        pthread_mutex_lock(&midi_lock);
        font = midi_font;
        midi_font = NULL;
        midi_font_path[0] = 0;
        pthread_mutex_unlock(&midi_lock);
        mh_sfont_free(font);
    }
    font = MH_IsOpen() ? mh_sfont_load(path, midi_samples, MH_MIDI_MAX_SAMPLES, err, sizeof err) : NULL;
    pthread_mutex_lock(&midi_lock);
    midi_font = font;
    midi_ok = font != NULL;
    midi_failed = font == NULL;
    if (font != NULL) {
        snprintf(midi_font_path, sizeof midi_font_path, "%s", path);
        if (midi != NULL) {
            mh_midi_set_font(midi, font);
        }
    }
    pthread_mutex_unlock(&midi_lock);
    midi_fade = 32;
    if (MH_IsOpen()) {
        write_ctrl();
    }
    if (font == NULL && MH_IsOpen() && named != NULL) {
        fprintf(stderr, "mister: SoundFont %s %s\n", path, err);
    } else if (font == NULL && MH_IsOpen()) {
        fprintf(stderr, "mister: SoundFont %s %s, FM music instead\n", path, err);
        // "has 140 MB of samples, more than ..."
        MH_OSDInfo(strncmp(err, "has ", 4) == 0 ? MH_INFO_SOUNDFONT_TOO_BIG : MH_INFO_SOUNDFONT_DAMAGED);
    } else if (font != NULL) {
        fprintf(stderr, "mister: SoundFont %s loaded\n", path);
    }
}

// The loader: the slot the OSD asks for, whenever it changes, or the file the
// program asks for
static void* midi_loader(void* arg) {
    char named[512];
    int slot, first = 1;

    (void)arg;
    MH_PinThreadToCPU1();
    pthread_mutex_lock(&midi_lock);
    for (;;) {
        while (!midi_quit && !midi_pending()) {
            pthread_cond_wait(&midi_cond, &midi_lock);
        }
        if (midi_quit) {
            break;
        }
        if (midi_named[0] != 0) {
            memcpy(named, midi_named, sizeof named);
            midi_tried = MIDI_NAMED;
            midi_tried_gen = midi_named_gen;
        } else {
            named[0] = 0;
            midi_tried = midi_slot;
        }
        slot = midi_slot;
        midi_loading = 1;
        midi_coming[0] = 0;
        pthread_mutex_unlock(&midi_lock);
        midi_load(slot, named[0] != 0 ? named : NULL, first);
        first = 0;
        pthread_mutex_lock(&midi_lock);
        midi_loading = 0;
    }
    pthread_mutex_unlock(&midi_lock);
    return NULL;
}

static void midi_off(void) {
    pthread_mutex_lock(&midi_lock);
    midi_enabled = 0;
    midi_to = MIDI_NONE;
    if (midi != NULL) {
        mh_midi_free(midi);
        midi = NULL;
    }
    pthread_mutex_unlock(&midi_lock);
    if (MH_IsOpen()) {
        write_ctrl();
    }
}

// At MH_Close(): the loader stopped, the font and the sample memory let go
static void midi_close(void) {
    midi_off();
    if (midi_thread_on) {
        pthread_mutex_lock(&midi_lock);
        midi_quit = 1;
        pthread_cond_broadcast(&midi_cond);
        pthread_mutex_unlock(&midi_lock);
        pthread_join(midi_thread, NULL);
        midi_thread_on = 0;
        midi_quit = 0;
    }
    if (midi_font != NULL) {
        mh_sfont_free(midi_font);
        midi_font = NULL;
    }
    midi_font_path[0] = 0;
    midi_tried = -1;
    midi_named[0] = 0;
    midi_failed = 0;
    midi_program_dest = MH_MIDI_OSD;
    midi_ok = 0;
    if (midi_samples != NULL) {
        munmap(midi_samples, MIDI_SAMPLES_SIZE);
        midi_samples = NULL;
    }
}

void MH_MidiEnable(int enabled) {
    void* p;
    int i;

    enabled = enabled != 0;
    if (!MH_MidiAvailable() || !audio_enabled || enabled == midi_enabled) {
        return;
    }
    if (!enabled) {
        // the notes off, then the core tells the MT32-pi to stop all its
        // notes as well when it sees MIDI off
        MH_MidiReset(0);
        for (i = 0; i < 50 && status[15] != fm_wr; i++) {
            sleep_ms(2);
        }
        midi_off();
        ring_stop();
        return;
    }
    if (midi_synth_there() && midi_samples == NULL) {
        p = mmap(NULL, MIDI_SAMPLES_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd,
            shm_is_file ? MIDI_SAMPLES_FILE : MIDI_SAMPLES_PHYS);
        if (p == MAP_FAILED) {
            // an MT32-pi can still play
            fprintf(stderr, "mister: cannot map the MIDI sample memory\n");
        } else {
            midi_samples = p;
        }
    }
    if (midi_samples != NULL) {
        // The core starts its read count from 0 when it has seen the
        // synthesizer off, at a vblank, as with the FM chip
        midi_wr = 0;
        write_ctrl();
        for (i = 0; i < 100 && status[36] != 0; i++) {
            sleep_ms(2);
        }
    }
    ring_start();
    pthread_mutex_lock(&midi_lock);
    if (midi_samples != NULL) {
        midi = mh_midi_new(midi_emit, NULL);
    }
    if (midi != NULL) {
        mh_midi_set_gain(midi, 0.5f);
        if (midi_font != NULL && midi_ok) {
            mh_midi_set_font(midi, midi_font);
        }
        midi_slot = (int)((MH_OSDStatusHigh() >> 24) & 7);
        midi_dropped = 0;
    }
    midi_enabled = 1;
    if (midi != NULL && !midi_thread_on && pthread_create(&midi_thread, NULL, midi_loader, NULL) == 0) {
        midi_thread_on = 1;
    }
    pthread_cond_signal(&midi_cond);
    pthread_mutex_unlock(&midi_lock);
    write_ctrl();
}

int MH_MidiWanted(void) {
    int dest = midi_dest(), wanted = dest == MIDI_PI;

    if (dest == MIDI_SYNTH) {
        pthread_mutex_lock(&midi_lock);
        wanted = midi_ok || midi_loading || midi_pending();
        pthread_mutex_unlock(&midi_lock);
    }
    return wanted;
}

int MH_MidiReady(void) {
    int ready;

    if (!MH_MidiWanted()) {
        return 0;
    }
    if (midi_dest() == MIDI_PI) {
        return 1;
    }
    pthread_mutex_lock(&midi_lock);
    ready = midi_ok && !midi_loading && !midi_pending();
    pthread_mutex_unlock(&midi_lock);
    return ready;
}

int MH_MidiPiConnected(void) {
    return MH_MidiAvailable() && (status[2] & STATUS_MT32) != 0;
}

void MH_MidiDest(int dest) {
    // a core before host version 17 sends to the Pi as its OSD says
    midi_program_dest = MH_IsOpen() && status[3] >= 17 && (dest == MH_MIDI_SYNTH || dest == MH_MIDI_PI) ? dest
                                                                                                         : MH_MIDI_OSD;
    if (MH_IsOpen()) {
        write_ctrl();
    }
}

int MH_MidiLoadSoundFont(const char* file) {
    if (!midi_synth_there()) {
        return 0;
    }
    pthread_mutex_lock(&midi_lock);
    if (file == NULL || file[0] == 0) {
        // the OSD's slot again: loaded again, unless it is the font there
        if (midi_named[0] != 0) {
            midi_named[0] = 0;
            midi_tried = -1;
        }
    } else {
        if (strchr(file, '/') != NULL) {
            snprintf(midi_named, sizeof midi_named, "%s", file);
        } else {
            snprintf(midi_named, sizeof midi_named, "%s/%s", MH_SoundFontDir(), file);
        }
        midi_named_gen++;
    }
    pthread_cond_signal(&midi_cond);
    pthread_mutex_unlock(&midi_lock);
    return 1;
}

int MH_MidiSoundFont(char* file, int size) {
    int state;

    pthread_mutex_lock(&midi_lock);
    if (midi_loading || midi_pending()) {
        state = MH_SF_LOADING;
    } else if (midi_ok) {
        state = MH_SF_LOADED;
    } else {
        state = midi_failed ? MH_SF_FAILED : MH_SF_NONE;
    }
    if (file != NULL && size > 0) {
        // the one coming, else the one there
        const char* path = state == MH_SF_LOADED ? midi_font_path
                         : state != MH_SF_LOADING ? ""
                         : midi_named[0] != 0 ? midi_named : midi_loading ? midi_coming : "";
        snprintf(file, (size_t)size, "%s", path);
    }
    pthread_mutex_unlock(&midi_lock);
    return state;
}

// the core counts the Pi's reports in status bits 24..27; the music going
// to the other of the two counts as well
int MH_MidiSynthChanged(void) {
    static unsigned last = ~0u, last_switches;
    unsigned reports;
    int changed;

    if (!MH_MidiAvailable()) {
        return 0;
    }
    reports = (status[2] >> 24) & 15;
    changed = reports != last || midi_switches != last_switches;
    last = reports;
    last_switches = midi_switches;
    return changed;
}

void MH_MidiVolume(int volume) {
    midi_volume = volume < 0 ? 0 : volume > 127 ? 127 : volume;
    if (MH_IsOpen()) {
        write_ctrl();
    }
}

static uint32_t midi_frame(int ahead) {
    return audio_wr + (uint32_t)(ahead < 0 ? 0 : ahead);
}

// n bytes of MIDI (1..3) in one entry of the FM ring, for the MT32-pi
static void pi_put(int ahead, const uint8_t* b, int n) {
    uint32_t at;

    if (!ring_put(ahead, (uint32_t)b[0] | (n > 1 ? (uint32_t)b[1] << 8 : 0) | (n > 2 ? (uint32_t)b[2] << 16 : 0)
            | ((uint32_t)n << 24))) {
        return;
    }
    pthread_mutex_lock(&ring_lock);
    at = audio_wr + (uint32_t)(ahead < 0 ? 0 : ahead);
    // the cable idle before the message's frame: it starts then
    if ((int32_t)(pi_free - at) < 0) {
        pi_free = at;
        pi_free_frac = 0;
    }
    pi_free_frac += (uint32_t)n * PI_BYTE_MILLIFRAMES;
    pi_free += pi_free_frac / 1000;
    pi_free_frac %= 1000;
    pthread_mutex_unlock(&ring_lock);
}

int MH_MidiBacklog(void) {
    int32_t left;

    if (midi_dest() != MIDI_PI) {
        return 0;
    }
    pthread_mutex_lock(&ring_lock);
    left = (int32_t)(pi_free - audio_wr);
    pthread_mutex_unlock(&ring_lock);
    return left > 0 ? left : 0;
}

static void pi_event(int ahead, int status_byte, int data1, int data2) {
    uint8_t b[3];

    b[0] = (uint8_t)status_byte;
    b[1] = (uint8_t)(data1 & 0x7f);
    b[2] = (uint8_t)(data2 & 0x7f);
    // program change and channel pressure have one data byte
    pi_put(ahead, b, (status_byte & 0xe0) == 0xc0 ? 2 : 3);
}

static void pi_reset(int ahead) {
    int ch;

    for (ch = 0; ch < 16; ch++) {
        pi_event(ahead, 0xb0 | ch, 120, 0); // all sound off
        pi_event(ahead, 0xb0 | ch, 123, 0); // all notes off (an MT-32 knows only this one)
        pi_event(ahead, 0xb0 | ch, 121, 0); // reset all controllers
    }
}

void MH_MidiEvent(int ahead, int status_byte, int data1, int data2) {
    int dest;

    if (!MH_IsOpen() || !midi_enabled || !audio_enabled || status_byte < 0x80 || status_byte >= 0xF0) {
        return;
    }
    dest = midi_dest();
    if (dest == MIDI_PI) {
        pi_event(ahead, status_byte, data1, data2);
    }
    // the synthesizer's notes only while it plays (the OSD may have just
    // turned to the FM chip: the game is about to turn to its FM music)
    if (dest != MIDI_SYNTH && (status_byte & 0xf0) == 0x90 && (data2 & 0x7f) > 0) {
        return;
    }
    pthread_mutex_lock(&midi_lock);
    if (midi != NULL) {
        mh_midi_event(midi, midi_frame(ahead), status_byte, data1, data2);
    }
    pthread_mutex_unlock(&midi_lock);
}

void MH_MidiSysEx(int ahead, const uint8_t* data, int length) {
    uint8_t b[3];
    int i, n;

    if (!MH_IsOpen() || !midi_enabled || !audio_enabled || length < 0) {
        return;
    }
    if (midi_dest() == MIDI_PI) {
        b[0] = 0xf0;
        n = 1;
        for (i = 0; i <= length; i++) {
            b[n++] = i < length ? (data[i] & 0x7f) : 0xf7;
            if (n == 3 || i == length) {
                pi_put(ahead, b, n);
                n = 0;
            }
        }
    }
    pthread_mutex_lock(&midi_lock);
    if (midi != NULL) {
        mh_midi_sysex(midi, midi_frame(ahead), data, length);
    }
    pthread_mutex_unlock(&midi_lock);
}

void MH_MidiReset(int ahead) {
    if (!MH_IsOpen() || !midi_enabled || !audio_enabled) {
        return;
    }
    if (midi_dest() == MIDI_PI) {
        pi_reset(ahead);
    }
    pthread_mutex_lock(&midi_lock);
    if (midi != NULL) {
        mh_midi_reset(midi, midi_frame(ahead));
    }
    pthread_mutex_unlock(&midi_lock);
}

// From MH_AudioWrite(), once a block: the voices the core has ended, the OSD's
// choice of SoundFont, and the commands of the block out to the core
static void midi_service(void) {
    static int stats = -1;
    static uint32_t dropped_told;
    static int told = -1;
    uint32_t bits[4], frame;
    uint64_t high;
    int i, dest, there;

    if (!midi_enabled) {
        midi_to = MIDI_NONE;
        return;
    }
    dest = midi_dest();
    there = (status[2] & STATUS_MT32) != 0;
    if (there * 4 + dest + 8 * (midi_program_dest != MH_MIDI_OSD) != told) {
        printf("mister: MIDI music %s%s\n",
            dest == MIDI_PI ? "to the MT32-pi" : dest == MIDI_SYNTH ? "to the synthesizer" : "off",
            !there || dest == MIDI_PI                 ? ""
            : midi_program_dest != MH_MIDI_OSD          ? ", the program's choice"
            : ((MH_OSDStatusHigh() >> 23) & 1)         ? " (FM chip in the OSD)"
                                                        : ", MT32-pi off in the OSD");
        told = there * 4 + dest + 8 * (midi_program_dest != MH_MIDI_OSD);
    }
    if (dest != midi_to) {
        if (midi_to == MIDI_PI) {
            pi_reset(0);
        }
        if (dest != MIDI_NONE) {
            midi_switches++;
        }
    }
    pthread_mutex_lock(&midi_lock);
    if (midi != NULL) {
        // the core writes them at a vblank: the frame again, for a set of one
        for (i = 0; i < 4; i++) {
            frame = status[42];
            bits[0] = status[38];
            bits[1] = status[39];
            bits[2] = status[40];
            bits[3] = status[41];
            if (status[42] == frame) {
                mh_midi_voices(midi, frame, bits);
                break;
            }
        }
        if (midi_to == MIDI_SYNTH && dest != MIDI_SYNTH) {
            // the music goes to the Pi, or the game turns to its FM music:
            // nothing of the synthesizer's goes on
            mh_midi_stop_all(midi, audio_wr);
        }
        high = MH_OSDStatusHigh();
        if ((int)((high >> 24) & 7) != midi_slot) {
            midi_slot = (int)((high >> 24) & 7);
            pthread_cond_signal(&midi_cond);
        }
        mh_midi_flush(midi);
    }
    pthread_mutex_unlock(&midi_lock);
    midi_to = dest;
    if (stats < 0) {
        stats = getenv("MISTER_HYBRID_STATS") != NULL;
    }
    if (stats && midi_dropped != dropped_told) {
        fprintf(stderr, "mister: MIDI ring full, %u commands dropped\n", midi_dropped);
        dropped_told = midi_dropped;
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

// The diagnostic report (mister_diag.c, docs/diagnostics.md)
void mh_diag_take(mh_diag_counts* c) {
    memset(c, 0, sizeof(*c));
    c->frames = __atomic_exchange_n(&diag.frames, 0, __ATOMIC_RELAXED);
    c->never = __atomic_exchange_n(&diag.never, 0, __ATOMIC_RELAXED);
    c->twice = __atomic_exchange_n(&diag.twice, 0, __ATOMIC_RELAXED);
    c->late_us = __atomic_exchange_n(&diag.late_us, 0, __ATOMIC_RELAXED);
    c->underruns = __atomic_exchange_n(&diag.underruns, 0, __ATOMIC_RELAXED);
    c->ahead = diag.ahead;
    if (!MH_IsOpen()) {
        return;
    }
    c->field = status[1];
    c->status2 = status[2];
    c->clock = status[14];
    c->osd = MH_OSDOpen();
    c->music = midi_to == MIDI_PI ? MH_DIAG_MUSIC_PI
        : midi_to == MIDI_SYNTH ? MH_DIAG_MUSIC_SYNTH
        : fm_enabled ? MH_DIAG_MUSIC_FM : MH_DIAG_MUSIC_NONE;
}

int mh_diag_frame(uint8_t** pixels, int* width, int* height, int* format, uint32_t palette[256]) {
    volatile uint32_t* pal;
    int bytes, i;

    if (!MH_IsOpen() || !(status[2] & STATUS_GAME) || (status[2] & STATUS_MSG)) {
        return 0;
    }
    bytes = frame_bytes();
    *pixels = malloc(bytes);
    if (*pixels == NULL) {
        return 0;
    }
    memcpy(*pixels, (const void*)framebuffer(fb_current), bytes);
    *width = MH_Width();
    *height = MH_Height();
    *format = fb_format;
    pal = (volatile uint32_t*)(shm + PALETTE_OFFSET + palette_slot * PALETTE_SLOT_SIZE);
    for (i = 0; i < 256; i++) {
        palette[i] = pal[i];
    }
    return 1;
}

int mh_diag_game_tid(void) {
    return (int)game_tid;
}
