// SPDX-License-Identifier: GPL-2.0-or-later
// Part of Hybrid_MiSTer (hps/LICENSE).
//
// The diagnostic report (docs/diagnostics.md): the OSD line "Save Diagnostic
// Report" (status bit 16, which the core flips at each pulse, host version
// 16) saves a zip in games/dnk/diagnostics/ with the setup and how the last
// 60 seconds went. A thread of its own keeps the record, a second to a
// slot, and writes the report, so the game needs no code for it and keeps
// running meanwhile. Only what bears on a problem goes in: no network
// settings, addresses, saved games or lists of the player's files.
//
// MISTER_HYBRID_DIAG_ROOT=<dir> stands for /media/fat (MiSTer.ini, the
// Main_MiSTer program, the report's folder), for tests on the PC;
// MISTER_HYBRID_DIAG_ALT=<n> for the number of the ini in use (0 MiSTer.ini).

#define _GNU_SOURCE
#include "mister_hybrid.h"
#include "mister_diag.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

// the host version this library was written for: raise it with the host version
#define LIBRARY_HOST 16
#define SLOTS 60
#define EXTRA_MAX 8

typedef struct {
    int used;
    mh_diag_counts c;
    uint32_t fields;
    int game_cpu, helper_cpu, cpu0, cpu1; // percent
} slot;

static pthread_t thread;
static int running;
static volatile int stopping;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static slot record[SLOTS];
static int record_next;

// MH_DiagAdd(): files a game adds to the report
static struct {
    char name[48];
    char* text;
} extra[EXTRA_MAX];

// --- a growing buffer ---

typedef struct {
    char* p;
    size_t n, cap;
} buf;

static void bput(buf* b, const void* data, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        char* p;

        while (cap < b->n + n + 1) {
            cap *= 2;
        }
        p = realloc(b->p, cap);
        if (p == NULL) {
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
    b->p[b->n] = 0;
}

static void bprintf(buf* b, const char* format, ...) {
    char line[1024];
    va_list ap;
    int n;

    va_start(ap, format);
    n = vsnprintf(line, sizeof line, format, ap);
    va_end(ap);
    if (n > 0) {
        bput(b, line, n < (int)sizeof line ? (size_t)n : sizeof line - 1);
    }
}

static void bu16(buf* b, unsigned v) {
    unsigned char x[2] = { v & 0xff, (v >> 8) & 0xff };
    bput(b, x, 2);
}

static void bu32(buf* b, uint32_t v) {
    unsigned char x[4] = { v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, v >> 24 };
    bput(b, x, 4);
}

static void bu32be(buf* b, uint32_t v) {
    unsigned char x[4] = { v >> 24, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff };
    bput(b, x, 4);
}

// A small file read whole (at most `max` bytes from its end), 0 if it is not there
static int read_file(const char* path, buf* out, long max) {
    FILE* f = fopen(path, "rb");
    char chunk[4096];
    size_t n;

    if (f == NULL) {
        return 0;
    }
    if (max > 0 && fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);

        fseek(f, size > max ? size - max : 0, SEEK_SET);
    }
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        bput(out, chunk, n);
    }
    fclose(f);
    return 1;
}

static const char* root(void) {
    const char* env = getenv("MISTER_HYBRID_DIAG_ROOT");

    return env != NULL && env[0] != 0 ? env : "/media/fat";
}

// --- CPU times ---

// utime + stime of /proc/<path>/stat
static long stat_ticks(const char* path, int* cpu, int* nice) {
    char line[1024];
    FILE* f = fopen(path, "r");
    char* p;
    unsigned long ut = 0, st = 0;
    long ni = 0;
    int processor = -1;

    if (f == NULL) {
        return -1;
    }
    if (fgets(line, sizeof line, f) == NULL) {
        fclose(f);
        return -1;
    }
    fclose(f);
    // after the name in parentheses: fields 3 (state) onwards
    p = strrchr(line, ')');
    if (p == NULL
        || sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %ld %*d %*d %*u %*u %*d %*u %*u "
                         "%*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*d %d",
                  &ut, &st, &ni, &processor) < 3) {
        return -1;
    }
    if (cpu != NULL) {
        *cpu = processor;
    }
    if (nice != NULL) {
        *nice = (int)ni;
    }
    return (long)(ut + st);
}

// busy and all ticks of cpu0 and cpu1 in /proc/stat
static void system_ticks(long long busy[2], long long all[2]) {
    char line[256];
    FILE* f = fopen("/proc/stat", "r");

    busy[0] = busy[1] = all[0] = all[1] = 0;
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        long long v[8] = { 0 };
        int n;

        if (sscanf(line, "cpu%d %lld %lld %lld %lld %lld %lld %lld %lld", &n, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                &v[6], &v[7]) >= 5 && n >= 0 && n < 2) {
            all[n] = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7];
            busy[n] = all[n] - v[3] - v[4]; // idle and iowait
        }
    }
    fclose(f);
}

// --- the record, a second to a slot ---

static void sample(int first) {
    static long last_game, last_all;
    static long long last_busy[2], last_total[2];
    static uint32_t last_field;
    long hz = sysconf(_SC_CLK_TCK);
    long long busy[2], total[2];
    char path[64];
    long game, all;
    slot s;
    int k;

    memset(&s, 0, sizeof s);
    mh_diag_take(&s.c);
    snprintf(path, sizeof path, "/proc/self/task/%d/stat", mh_diag_game_tid());
    game = stat_ticks(path, NULL, NULL);
    all = stat_ticks("/proc/self/stat", NULL, NULL);
    system_ticks(busy, total);
    if (!first) {
        s.used = 1;
        s.fields = s.c.field - last_field;
        s.game_cpu = (int)((game - last_game) * 100 / hz);
        // the two are read a moment apart
        s.helper_cpu = (int)(((all - last_all) - (game - last_game)) * 100 / hz);
        if (s.helper_cpu < 0) {
            s.helper_cpu = 0;
        }
        for (k = 0; k < 2; k++) {
            long long t = total[k] - last_total[k];
            int pct = t > 0 ? (int)((busy[k] - last_busy[k]) * 100 / t) : 0;

            if (k == 0) {
                s.cpu0 = pct;
            } else {
                s.cpu1 = pct;
            }
        }
        pthread_mutex_lock(&lock);
        record[record_next] = s;
        record_next = (record_next + 1) % SLOTS;
        pthread_mutex_unlock(&lock);
    }
    last_field = s.c.field;
    last_game = game;
    last_all = all;
    memcpy(last_busy, busy, sizeof last_busy);
    memcpy(last_total, total, sizeof last_total);
}

// --- zip (stored) and PNG ---

static uint32_t crc32_of(const void* data, size_t n, uint32_t crc) {
    static uint32_t table[256];
    const unsigned char* p = data;
    size_t i;

    if (table[1] == 0) {
        for (i = 0; i < 256; i++) {
            uint32_t c = (uint32_t)i;
            int k;

            for (k = 0; k < 8; k++) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
    }
    crc = ~crc;
    for (i = 0; i < n; i++) {
        crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

static void png_chunk(buf* png, const char* type, const buf* data) {
    uint32_t crc = crc32_of(type, 4, 0);

    if (data != NULL) {
        crc = crc32_of(data->p, data->n, crc);
    }
    bu32be(png, data != NULL ? (uint32_t)data->n : 0);
    bput(png, type, 4);
    if (data != NULL) {
        bput(png, data->p, data->n);
    }
    bu32be(png, crc);
}

// The frame as a PNG: deflate's stored blocks, no compression needed
static void make_png(buf* png, const uint8_t* pixels, int width, int height, int format, const uint32_t* palette) {
    int indexed = format == MH_FORMAT_INDEX8;
    int row = width * (indexed ? 1 : 3) + 1;
    buf ihdr = { 0 }, plte = { 0 }, raw = { 0 }, idat = { 0 };
    uint32_t a = 1, b = 0;
    unsigned char* line = malloc(row);
    size_t at;
    int x, y;

    if (line == NULL) {
        return;
    }
    for (y = 0; y < height; y++) {
        line[0] = 0; // no filter
        for (x = 0; x < width; x++) {
            if (indexed) {
                line[1 + x] = pixels[y * width + x];
            } else {
                unsigned v = pixels[2 * (y * width + x)] | pixels[2 * (y * width + x) + 1] << 8;

                line[1 + 3 * x] = (unsigned char)(((v >> 11) & 31) * 255 / 31);
                line[2 + 3 * x] = (unsigned char)(((v >> 5) & 63) * 255 / 63);
                line[3 + 3 * x] = (unsigned char)((v & 31) * 255 / 31);
            }
        }
        bput(&raw, line, row);
    }
    free(line);
    for (at = 0; at < raw.n; at++) {
        a = (a + (unsigned char)raw.p[at]) % 65521;
        b = (b + a) % 65521;
    }
    bput(&idat, "\x78\x01", 2);
    for (at = 0; at < raw.n || at == 0;) {
        size_t n = raw.n - at > 65535 ? 65535 : raw.n - at;
        unsigned char head = at + n == raw.n ? 1 : 0;

        bput(&idat, &head, 1);
        bu16(&idat, (unsigned)n);
        bu16(&idat, (unsigned)~n & 0xffff);
        bput(&idat, raw.p + at, n);
        at += n;
        if (n == 0) {
            break;
        }
    }
    bu32be(&idat, b << 16 | a);

    bput(png, "\x89PNG\r\n\x1a\n", 8);
    bu32be(&ihdr, (uint32_t)width);
    bu32be(&ihdr, (uint32_t)height);
    bput(&ihdr, indexed ? "\x08\x03\x00\x00\x00" : "\x08\x02\x00\x00\x00", 5);
    png_chunk(png, "IHDR", &ihdr);
    if (indexed) {
        for (x = 0; x < 256; x++) {
            unsigned char rgb[3] = { palette[x] >> 16, palette[x] >> 8, palette[x] };

            bput(&plte, rgb, 3);
        }
        png_chunk(png, "PLTE", &plte);
    }
    png_chunk(png, "IDAT", &idat);
    png_chunk(png, "IEND", NULL);
    free(ihdr.p);
    free(plte.p);
    free(raw.p);
    free(idat.p);
}

typedef struct {
    buf data, central;
    int count;
    unsigned dos_time, dos_date;
} zip;

static void zip_add(zip* z, const char* name, const void* data, size_t n) {
    uint32_t crc = crc32_of(data, n, 0);
    uint32_t offset = (uint32_t)z->data.n;
    size_t len = strlen(name);

    bu32(&z->data, 0x04034b50);
    bu16(&z->data, 20);
    bu16(&z->data, 0);
    bu16(&z->data, 0); // stored
    bu16(&z->data, z->dos_time);
    bu16(&z->data, z->dos_date);
    bu32(&z->data, crc);
    bu32(&z->data, (uint32_t)n);
    bu32(&z->data, (uint32_t)n);
    bu16(&z->data, (unsigned)len);
    bu16(&z->data, 0);
    bput(&z->data, name, len);
    bput(&z->data, data, n);

    bu32(&z->central, 0x02014b50);
    bu16(&z->central, 20);
    bu16(&z->central, 20);
    bu16(&z->central, 0);
    bu16(&z->central, 0);
    bu16(&z->central, z->dos_time);
    bu16(&z->central, z->dos_date);
    bu32(&z->central, crc);
    bu32(&z->central, (uint32_t)n);
    bu32(&z->central, (uint32_t)n);
    bu16(&z->central, (unsigned)len);
    bu16(&z->central, 0);
    bu16(&z->central, 0);
    bu16(&z->central, 0);
    bu16(&z->central, 0);
    bu32(&z->central, 0);
    bu32(&z->central, offset);
    bput(&z->central, name, len);
    z->count++;
}

static void zip_end(zip* z) {
    uint32_t at = (uint32_t)z->data.n;

    bput(&z->data, z->central.p, z->central.n);
    bu32(&z->data, 0x06054b50);
    bu16(&z->data, 0);
    bu16(&z->data, 0);
    bu16(&z->data, (unsigned)z->count);
    bu16(&z->data, (unsigned)z->count);
    bu32(&z->data, (uint32_t)z->central.n);
    bu32(&z->data, at);
    bu16(&z->data, 0);
}

// --- what the report says ---

static const char* music_name(int m) {
    static const char* names[] = { "none", "FM chip", "synthesizer", "MT32-pi" };

    return m >= 0 && m < 4 ? names[m] : "?";
}

static const char* mode_name(uint32_t status2) {
    static char name[48];
    static const short modes[16][2] = { { 320, 200 }, { 640, 200 }, { 640, 400 }, { 320, 240 }, { 640, 480 },
        { 800, 600 }, { 1024, 768 }, { 640, 240 } };
    int m = (status2 >> 12) & 15;

    snprintf(name, sizeof name, "%dx%d%s %s", modes[m][0], modes[m][1],
        status2 & (1u << 9) ? (status2 & (1u << 31) ? "i at field rate" : "i") : "",
        status2 & (1u << 18) ? "31kHz" : "15kHz");
    return name;
}

// Main_MiSTer's build date: the "$VER:yymmdd" in its program
static void main_version(char* out, size_t size) {
    char path[256];
    buf b = { 0 };
    size_t i;

    snprintf(out, size, "unknown");
    snprintf(path, sizeof path, "%s/MiSTer", root());
    if (!read_file(path, &b, 0)) {
        return;
    }
    for (i = 0; i + 11 <= b.n; i++) {
        if (memcmp(b.p + i, "$VER:", 5) == 0) {
            snprintf(out, size, "%.6s", b.p + i + 5);
            break;
        }
    }
    free(b.p);
}

static void first_line(const char* path, char* out, size_t size) {
    FILE* f = fopen(path, "r");

    out[0] = 0;
    if (f != NULL) {
        if (fgets(out, (int)size, f) == NULL) {
            out[0] = 0;
        }
        fclose(f);
    }
    out[strcspn(out, "\r\n")] = 0;
}

// The video, audio and input keys of MiSTer.ini (Main_MiSTer's cfg.cpp)
// that bear on a port, in the sections that apply to it; every other key
// and section is left out
static const char* const ini_keys[] = { "video_mode", "video_mode_pal", "video_mode_ntsc", "vsync_adjust",
    "vscale_mode", "vscale_border", "direct_video", "forced_scandoubler", "composite_sync", "ypbpr", "vga_scaler",
    "vga_sog", "vga_mode", "ntsc_mode", "dvi_mode", "hdmi_limited", "hdmi_audio_96k", "hdmi_game_mode", "hdmi_off",
    "refresh_min", "refresh_max", "vrr_mode", "vrr_vesa_framerate", "video_info", "video_off", "fb_size",
    "fb_terminal", "menu_pal", "hdr", "custom_aspect_ratio_1", "custom_aspect_ratio_2", "afilter_default",
    "vfilter_default", "vfilter_vertical_default", "vfilter_scanlines_default", "vfilter_interlace_default",
    "shmask_default", "shmask_mode_default", "preset_default", "video_brightness", "video_contrast",
    "video_saturation", "video_hue", "video_gain_offset", "deadzone", "gamepad_defaults", "rumble", "wheel_force",
    "wheel_range", "kbd_nomouse", "mouse_throttle", "controller_unique_mapping", "lookahead", "main", "waitmount",
    "spd_quirk", "debug", "log_file_entry", NULL };

// [MiSTer], [video=...] and the sections named after the core (its name,
// set name or file name; "name*" for every name it starts)
static int section_applies(const char* name, const char* const* names) {
    const char* star = strchr(name, '*');
    size_t n = star ? (size_t)(star - name) : strlen(name);
    int i;

    if (strcasecmp(name, "MiSTer") == 0 || strncasecmp(name, "video=", 6) == 0) {
        return 1;
    }
    for (i = 0; names[i] != NULL; i++) {
        if (names[i][0] && (star ? strncasecmp(name, names[i], n) == 0 : strcasecmp(name, names[i]) == 0)) {
            return 1;
        }
    }
    return 0;
}

// The ini Main_MiSTer reads, of MiSTer.ini and the alternatives it offers:
// their number is in the HPS memory it keeps across core loads (altcfg() in
// its user_io.cpp), 1..3 the first three MiSTer_*.ini of the folder's
// listing in the order of their names (cfg_get_name() in its cfg.cpp).
// MISTER_HYBRID_DIAG_ALT=<n> stands for that number, for tests on the PC.
static void ini_in_use(char* name, size_t size) {
    char names[3][64] = { "", "", "" }, t[64];
    const char* env = getenv("MISTER_HYBRID_DIAG_ALT");
    int alt = 0, n = 0;
    DIR* d;
    struct dirent* e;

    snprintf(name, size, "MiSTer.ini");
    if (env != NULL) {
        alt = atoi(env);
    } else {
        int fd = open("/dev/mem", O_RDONLY | O_SYNC);

        if (fd >= 0) {
            volatile uint8_t* p = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, 0x1FFFF000);

            if (p != MAP_FAILED) {
                if (p[0xF04] == 0x34 && p[0xF05] == 0x99 && p[0xF06] == 0xBA) {
                    alt = p[0xF07];
                }
                munmap((void*)p, 0x1000);
            }
            close(fd);
        }
    }
    if (alt < 1 || alt > 3) {
        return;
    }
    d = opendir(root());
    while (d != NULL && n < 3 && (e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);

        if (strncasecmp(e->d_name, "MiSTer_", 7) == 0 && len > 4 && strcasecmp(e->d_name + len - 4, ".ini") == 0) {
            snprintf(names[n++], sizeof names[0], "%s", e->d_name);
        }
    }
    if (d != NULL) {
        closedir(d);
    }
    for (int i = 0; i < n; i++) {
        for (int j = 1; j < n - i; j++) {
            if (strcasecmp(names[j - 1], names[j]) > 0) {
                memcpy(t, names[j - 1], sizeof t);
                memcpy(names[j - 1], names[j], sizeof t);
                memcpy(names[j], t, sizeof t);
            }
        }
    }
    if (names[alt - 1][0]) {
        snprintf(name, size, "%s", names[alt - 1]);
    }
}

static void ini_lines(buf* out, const char* file, const char* const* names) {
    char path[256], header[80], line[512];
    FILE* f;
    int applies = 1, shown = -1;

    snprintf(path, sizeof path, "%s/%s", root(), file);
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    bprintf(out, "\n%s:\n", file);
    while (fgets(line, sizeof line, f) != NULL) {
        char* p = line;
        char key[64];
        int k, n = 0;

        line[strcspn(line, "\r\n")] = 0;
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '[') {
            char* end = strchr(p, ']');

            if (end != NULL) {
                *end = 0;
                applies = section_applies(p + 1, names);
                if (applies) {
                    // the header only once something of it is shown
                    snprintf(header, sizeof header, "[%.76s]", p + 1);
                    shown = 0;
                }
            }
            continue;
        }
        if (!applies || *p == ';' || *p == '#') {
            continue;
        }
        while (n < (int)sizeof key - 1 && (isalnum((unsigned char)p[n]) || p[n] == '_')) {
            key[n] = (char)tolower((unsigned char)p[n]);
            n++;
        }
        key[n] = 0;
        for (k = 0; ini_keys[k] != NULL && strcmp(ini_keys[k], key) != 0; k++) {
        }
        if (ini_keys[k] == NULL) {
            continue;
        }
        if (shown == 0) {
            bprintf(out, "%s\n", header);
            shown = 1;
        }
        // the value without a comment after it
        p[strcspn(p, ";#")] = 0;
        n = (int)strlen(p);
        while (n > 0 && isspace((unsigned char)p[n - 1])) {
            p[--n] = 0;
        }
        bprintf(out, "  %s\n", p);
    }
    fclose(f);
}

static void space(buf* out, const char* label, const char* path) {
    struct statvfs v;

    if (statvfs(path, &v) == 0) {
        bprintf(out, "%s: %.0f MB free of %.0f MB\n", label, (double)v.f_bavail * v.f_frsize / 1048576.0,
            (double)v.f_blocks * v.f_frsize / 1048576.0);
    }
}

static long free_mb(const char* path) {
    struct statvfs v;

    return statvfs(path, &v) == 0 ? (long)((double)v.f_bavail * v.f_frsize / 1048576.0) : -1;
}

// The framework's OSD lines (template/core/@NAME@.sv), by name
static void osd_text(buf* out, uint64_t low, uint64_t high) {
    static const char* const scandoubler[] = { "None", "HQ2x", "CRT 25%", "CRT 50%", "CRT 75%", "?", "?", "?" };
    static const char* const mix[] = { "None", "25%", "50%", "100%" };
    static const char* const aspect[] = { "Original", "Full Screen", "[ARC1]", "[ARC2]" };
    static const char* const scale[] = { "Normal", "V-Integer", "Narrower HV-Integer", "Wider HV-Integer",
        "HV-Integer", "?", "?", "?" };
    static const char* const pi_rom[] = { "MT-32 v1", "MT-32 v2", "CM-32L", "?" };
    int size = (int)(high & 31), hpos = (int)((high >> 5) & 15), vpos = (int)((high >> 9) & 15);
    int pi_sf = (int)((high >> 36) & 15);
    int top, bottom;

    MH_HudMargins(&top, &bottom);
    bprintf(out, "The port's OSD settings (status bits)\n\n");
    bprintf(out, "Aspect ratio        %s\n", aspect[(high >> 57) & 3]);
    bprintf(out, "Scale               %s\n", scale[(high >> 59) & 7]);
    bprintf(out, "Scandoubler Fx      %s\n", scandoubler[(low >> 2) & 7]);
    bprintf(out, "HDMI Only           %s\n", (low >> 1) & 1 ? "Yes" : "No");
    bprintf(out, "Horizontal Size     %+d\n", size < 8 ? size : size - 32);
    bprintf(out, "Horizontal Pos      %+d\n", hpos < 8 ? hpos : hpos - 16);
    bprintf(out, "Vertical Pos        %+d\n", vpos < 8 ? vpos : vpos - 16);
    bprintf(out, "HUD (Top)           %d pixels (entry %d)\n", top, (int)((high >> 13) & 31));
    bprintf(out, "HUD (Bottom)        %d pixels (entry %d)\n", bottom, (int)((high >> 18) & 31));
    bprintf(out, "Stereo Mix          %s\n", mix[(low >> 5) & 3]);
    bprintf(out, "FPS Counter         %s\n", (low >> 15) & 1 ? "On" : "Off");
    bprintf(out, "Bits 7..14          %x (Sound / Music Volume where the game has them)\n", (unsigned)((low >> 7) & 0xff));
    bprintf(out, "Music               %s\n", (high >> 23) & 1 ? "FM chip" : "MIDI");
    if ((high >> 24) & 7) {
        bprintf(out, "SoundFont           %d\n", (int)((high >> 24) & 7));
    } else {
        bprintf(out, "SoundFont           Default\n");
    }
    bprintf(out, "MT32-pi             %s\n", (high >> 32) & 1 ? "Off" : "On");
    bprintf(out, "MT32-pi Synth       %s\n", (high >> 33) & 1 ? "MT-32" : "SoundFont");
    bprintf(out, "MT32-pi ROM         %s\n", pi_rom[(high >> 34) & 3]);
    if (pi_sf == 0) {
        bprintf(out, "MT32-pi SoundFont   Pi's own\n");
    } else {
        bprintf(out, "MT32-pi SoundFont   %d\n", pi_sf - 1);
    }
    bprintf(out, "\nThe game's own options, bits 24..63: %010llx\n", (unsigned long long)(low >> 24));
    bprintf(out, "Bits 0..63:   %016llx\nBits 64..127: %016llx\n", (unsigned long long)low, (unsigned long long)high);
}

// The processes using the most CPU over a second (names only)
typedef struct {
    int pid, cpu, nice;
    long ticks;
    char name[32];
} proc;

static int read_procs(proc* list, int max) {
    DIR* d = opendir("/proc");
    struct dirent* e;
    int n = 0;

    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && n < max) {
        char path[300], line[256];
        FILE* f;
        char *a, *b;

        if (!isdigit((unsigned char)e->d_name[0])) {
            continue;
        }
        snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
        list[n].ticks = stat_ticks(path, &list[n].cpu, &list[n].nice);
        if (list[n].ticks < 0 || (f = fopen(path, "r")) == NULL) {
            continue;
        }
        if (fgets(line, sizeof line, f) == NULL) {
            line[0] = 0;
        }
        fclose(f);
        a = strchr(line, '(');
        b = strrchr(line, ')');
        if (a == NULL || b == NULL || b < a) {
            continue;
        }
        snprintf(list[n].name, sizeof list[n].name, "%.*s", (int)(b - a - 1), a + 1);
        list[n].pid = atoi(e->d_name);
        n++;
    }
    closedir(d);
    return n;
}

static int by_ticks(const void* a, const void* b) {
    return (int)(((const proc*)b)->ticks - ((const proc*)a)->ticks);
}

static void top_processes(buf* out) {
    static proc before[1024], after[1024];
    long hz = sysconf(_SC_CLK_TCK);
    int nb = read_procs(before, 1024), na, i, j, shown = 0;

    sleep(1);
    na = read_procs(after, 1024);
    for (i = 0; i < na; i++) {
        long was = 0;

        for (j = 0; j < nb; j++) {
            if (before[j].pid == after[i].pid) {
                was = before[j].ticks;
                break;
            }
        }
        after[i].ticks -= was;
    }
    qsort(after, na, sizeof after[0], by_ticks);
    bprintf(out, "\nProcesses using the most CPU over a second (%% of one CPU, the CPU it last ran on, nice):\n");
    for (i = 0; i < na && shown < 10; i++) {
        if (after[i].ticks <= 0 && shown >= 3) {
            break;
        }
        bprintf(out, "  %3ld%%  cpu%d  %3d  %s%s\n", after[i].ticks * 100 / hz, after[i].cpu, after[i].nice,
            after[i].name, after[i].pid == getpid() ? " (this game)" : "");
        shown++;
    }
}

static void system_text(buf* out) {
    struct utsname u;
    char line[256];
    FILE* f;
    double up = 0;

    if (uname(&u) == 0) {
        bprintf(out, "Kernel: %s %s %s\n", u.sysname, u.release, u.machine);
    }
    first_line("/proc/uptime", line, sizeof line);
    sscanf(line, "%lf", &up);
    bprintf(out, "Up for %.0f minutes\n", up / 60);
    first_line("/proc/loadavg", line, sizeof line);
    bprintf(out, "Load: %s\n", line);
    f = fopen("/proc/meminfo", "r");
    if (f != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            if (strncmp(line, "MemTotal:", 9) == 0 || strncmp(line, "MemFree:", 8) == 0
                || strncmp(line, "MemAvailable:", 13) == 0 || strncmp(line, "SwapTotal:", 10) == 0) {
                bprintf(out, "%s", line);
            }
        }
        fclose(f);
    }
    first_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", line, sizeof line);
    if (line[0]) {
        bprintf(out, "CPU clock: %s kHz\n", line);
    } else {
        bprintf(out, "CPU clock: not reported\n");
    }
    top_processes(out);

    // controllers: names and ids only (never "Uniq" or "Phys", which hold
    // Bluetooth addresses)
    bprintf(out, "\nInput devices (bus, vendor, product: name):\n");
    f = fopen("/proc/bus/input/devices", "r");
    if (f != NULL) {
        char ids[96] = "";

        while (fgets(line, sizeof line, f) != NULL) {
            unsigned bus, vendor, product;

            line[strcspn(line, "\n")] = 0;
            if (sscanf(line, "I: Bus=%x Vendor=%x Product=%x", &bus, &vendor, &product) == 3) {
                snprintf(ids, sizeof ids, "%04x %04x %04x", bus, vendor, product);
            } else if (strncmp(line, "N: Name=", 8) == 0) {
                bprintf(out, "  %s: %s\n", ids, line + 8);
            }
        }
        fclose(f);
    }

    // the SD card and USB drives, how they are mounted (no network shares)
    bprintf(out, "\nDrives:\n");
    f = fopen("/proc/mounts", "r");
    if (f != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            char dev[128], at[128], type[32], options[256];

            if (sscanf(line, "%127s %127s %31s %255s", dev, at, type, options) == 4
                && (strcmp(at, "/media/fat") == 0 || strncmp(at, "/media/usb", 10) == 0)
                && strncmp(dev, "/dev/", 5) == 0) {
                bprintf(out, "  %s %s %s %s%s%s\n", at, dev, type, strstr(options, "rw") == options ? "rw" : "ro",
                    strstr(options, "sync") && !strstr(options, "async") ? " sync" : "",
                    strstr(options, "noexec") ? " noexec" : "");
            }
        }
        fclose(f);
    }
}

// --- the report ---

static int write_report(void) {
    slot snap[SLOTS];
    char core[64], rbf[96], rbf_base[96], path[512], tmp[520], name[160], mainver[16], cwd[256];
    buf summary = { 0 }, csv = { 0 }, setup = { 0 }, osd = { 0 }, sys = { 0 }, log = { 0 }, png = { 0 };
    zip z = { 0 };
    uint64_t low = MH_OSDStatus(), high = MH_OSDStatusHigh();
    uint8_t* pixels = NULL;
    uint32_t palette[256];
    int width = 0, height = 0, format = 0, have_frame;
    time_t now = time(NULL);
    struct tm tm;
    struct stat st;
    int i, n = 0, first, ok;
    long sd_free;
    // the 60 seconds in a few numbers
    unsigned fields = 0, frames = 0, never = 0, twice = 0, underruns = 0, late_max = 0, max_game = 0;
    unsigned sum_game = 0, sum_cpu0 = 0, sum_cpu1 = 0, sum_others0 = 0, ahead = 0;
    int music_seen[4] = { 0 };
    FILE* f;

    // the moment the line was picked: the frame first, then the record
    have_frame = mh_diag_frame(&pixels, &width, &height, &format, palette);
    pthread_mutex_lock(&lock);
    for (i = 0; i < SLOTS; i++) {
        snap[i] = record[(record_next + i) % SLOTS];
    }
    pthread_mutex_unlock(&lock);

    localtime_r(&now, &tm);
    snprintf(core, sizeof core, "%s", MH_CoreName()[0] ? MH_CoreName() : "hybrid");
    first_line("/tmp/RBFNAME", rbf, sizeof rbf);
    main_version(mainver, sizeof mainver);
    if (getcwd(cwd, sizeof cwd) == NULL) {
        snprintf(cwd, sizeof cwd, "?");
    }

    // pacing.csv
    bprintf(&csv, "second,fields,frames,never_shown,shown_twice,latest_ms,game_cpu,helper_cpu,cpu0,cpu1,"
                  "audio_underruns,music,mode,osd_open\n");
    for (i = 0; i < SLOTS; i++) {
        if (snap[i].used) {
            n++;
        }
    }
    first = SLOTS - n;
    for (i = first; i < SLOTS; i++) {
        slot* s = &snap[i];

        bprintf(&csv, "%d,%u,%u,%u,%u,%.1f,%d,%d,%d,%d,%u,%s,%s,%d\n", i - (SLOTS - 1), s->fields, s->c.frames,
            s->c.never, s->c.twice, s->c.late_us / 1000.0, s->game_cpu, s->helper_cpu, s->cpu0, s->cpu1,
            s->c.underruns, music_name(s->c.music), mode_name(s->c.status2), s->c.osd);
        fields += s->fields;
        frames += s->c.frames;
        never += s->c.never;
        twice += s->c.twice;
        underruns += s->c.underruns;
        if (s->c.late_us > late_max) {
            late_max = s->c.late_us;
        }
        if ((unsigned)s->game_cpu > max_game) {
            max_game = (unsigned)s->game_cpu;
        }
        sum_game += (unsigned)s->game_cpu;
        sum_cpu0 += (unsigned)s->cpu0;
        sum_cpu1 += (unsigned)s->cpu1;
        // CPU0 busy with something other than the game thread (it has CPU0 alone)
        sum_others0 += s->cpu0 > s->game_cpu ? (unsigned)(s->cpu0 - s->game_cpu) : 0;
        ahead |= s->c.ahead;
        if (s->c.music < 4) {
            music_seen[s->c.music] = 1;
        }
    }

    // setup.txt
    bprintf(&setup, "Port: %s, set name %s\n", core, MH_SetName());
    bprintf(&setup, "Core file: %s\n", rbf[0] ? rbf : "?");
    bprintf(&setup, "Host version of the core: %d\n", MH_HostVersion());
    bprintf(&setup, "Library: written for host version %d, built %s %s\n", LIBRARY_HOST, __DATE__, __TIME__);
    bprintf(&setup, "Main_MiSTer: %s\n", mainver);
    if (n > 0) {
        mh_diag_counts* c = &snap[SLOTS - 1].c;

        bprintf(&setup, "Video: %s, %s; clock asked %.1f MHz, measured %.3f MHz\n", mode_name(c->status2),
            c->status2 & (1u << 17) ? "shown" : "off", (c->clock & 0xff) / 2.0, (c->clock >> 16) / 80.0);
        bprintf(&setup, "Fields per second: %.2f\n", (double)fields / n);
    }
    bprintf(&setup, "Game data: %s\n", MH_GamePath());
    space(&setup, "Free space where the game runs", cwd);
    space(&setup, "Free space on the SD card", root());
    {
        // the game thread's, not this one's
        cpu_set_t cpus;
        int nice = getpriority(PRIO_PROCESS, (id_t)mh_diag_game_tid());

        CPU_ZERO(&cpus);
        sched_getaffinity(mh_diag_game_tid(), sizeof cpus, &cpus);
        bprintf(&setup, "Game thread on %s, nice %d\n", CPU_ISSET(1, &cpus) && CPU_ISSET(0, &cpus) ? "CPU0 and CPU1" :
            CPU_ISSET(1, &cpus) ? "CPU1" : "CPU0", nice);
    }
    bprintf(&setup, "Environment:");
    {
        extern char** environ;
        char** e;

        for (e = environ; *e != NULL; e++) {
            if (strncmp(*e, "MISTER_HYBRID_", 14) == 0 || strncmp(*e, "SDL_", 4) == 0) {
                bprintf(&setup, " %s", *e);
            }
        }
    }
    bprintf(&setup, "\n");
    // the core file's name without its date, as Main_MiSTer matches sections
    snprintf(rbf_base, sizeof rbf_base, "%s", rbf);
    if (strrchr(rbf_base, '_') != NULL) {
        *strrchr(rbf_base, '_') = 0;
    }
    const char* names[] = { core, MH_SetName(), rbf, rbf_base, NULL };

    // only the ini Main_MiSTer reads: it never reads two
    {
        char ini[64];

        ini_in_use(ini, sizeof ini);
        ini_lines(&setup, ini, names);
    }

    osd_text(&osd, low, high);
    system_text(&sys);

    // the game's log of this session: what it printed so far (the launch
    // script sends it to a file in /tmp)
    {
        char target[256];
        ssize_t len;

        fflush(stdout);
        len = readlink("/proc/self/fd/2", target, sizeof target - 1);
        if (len > 0) {
            target[len] = 0;
            if (stat(target, &st) == 0 && S_ISREG(st.st_mode)) {
                bprintf(&log, "%s, its last 64 KB:\n\n", target);
                read_file(target, &log, 65536);
            }
        }
        if (log.n == 0) {
            bprintf(&log, "(the game's output goes to no file)\n");
        }
    }

    // summary.txt
    sd_free = free_mb(root());
    bprintf(&summary, "Diagnostic report of %s (%s), %04d-%02d-%02d %02d:%02d:%02d\n\n", core, MH_SetName(),
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    bprintf(&summary, "Core %s, host version %d; Main_MiSTer %s\n", rbf[0] ? rbf : "?", MH_HostVersion(), mainver);
    if (n > 0) {
        mh_diag_counts* c = &snap[SLOTS - 1].c;

        bprintf(&summary, "Video %s, %.2f fields per second\n", mode_name(c->status2), (double)fields / n);
    }
    bprintf(&summary, "Game data in %s\n\n", MH_GamePath());
    bprintf(&summary, "The last %d seconds (pacing.csv has them second by second):\n", n);
    bprintf(&summary, "  frames %u in %u fields; never shown %u, shown twice %u\n", frames, fields, never, twice);
    bprintf(&summary, "  the latest a frame came in its field: %.1f ms\n", late_max / 1000.0);
    if (n > 0) {
        bprintf(&summary, "  game thread %u%% of CPU0 on average, %u%% at most\n", sum_game / n, max_game);
        bprintf(&summary, "  CPU0 busy %u%%, CPU1 %u%% on average\n", sum_cpu0 / n, sum_cpu1 / n);
    }
    bprintf(&summary, "  audio ring ran dry %u times\n", underruns);
    bprintf(&summary, "  music: %s%s%s%s\n", music_seen[3] ? "MT32-pi " : "", music_seen[2] ? "synthesizer " : "",
        music_seen[1] ? "FM chip " : "", music_seen[0] ? "none" : "");
    bprintf(&summary, "  drawing a field ahead: %s\n", ahead ? "yes" : "no");

    bprintf(&summary, "\nWhat stands out:\n");
    ok = 1;
    if (never + twice > 0) {
        bprintf(&summary, "- %u frames never shown and %u shown twice: the picture stutters", never, twice);
        if (n > 0) {
            int worst = -1;
            unsigned most = 0;

            for (i = first; i < SLOTS; i++) {
                if (snap[i].c.never + snap[i].c.twice > most) {
                    most = snap[i].c.never + snap[i].c.twice;
                    worst = i;
                }
            }
            if (worst >= 0) {
                bprintf(&summary, ", most at second %d (%u)", worst - (SLOTS - 1), most);
            }
        }
        bprintf(&summary, "\n");
        ok = 0;
    }
    if (n > 0 && sum_others0 / n >= 10) {
        bprintf(&summary, "- something besides the game thread kept CPU0 %u%% busy on average (system.txt lists "
                          "the processes)\n", sum_others0 / n);
        ok = 0;
    }
    if (n > 0 && max_game >= 95) {
        bprintf(&summary, "- the game thread had the CPU all the time at least once (%u%%)\n", max_game);
        ok = 0;
    }
    if (underruns > 0) {
        bprintf(&summary, "- the sound broke up: the audio ring ran dry %u times\n", underruns);
        ok = 0;
    }
    if (strncmp(cwd, "/media/usb", 10) == 0) {
        bprintf(&summary, "- the game's data is on a USB drive (%s)\n", MH_GamePath());
    }
    if (sd_free >= 0 && sd_free < 200) {
        bprintf(&summary, "- the SD card is nearly full: %ld MB free\n", sd_free);
        ok = 0;
    }
    if (getenv("MISTER_HYBRID_AHEAD") || getenv("MISTER_HYBRID_PACE") || getenv("MISTER_HYBRID_HUD")) {
        bprintf(&summary, "- test settings in the environment (setup.txt)\n");
    }
    if (n > 0 && (snap[SLOTS - 1].c.status2 & (1u << 9))) {
        bprintf(&summary, (snap[SLOTS - 1].c.status2 & (1u << 31)) ? "- an interlaced mode at field rate: a frame every field\n"
                                                                   : "- an interlaced mode: a frame every second field\n");
    }
    if (n < SLOTS) {
        bprintf(&summary, "- the game ran only %d seconds before the report\n", n);
    }
    if (ok) {
        bprintf(&summary, "- nothing in the numbers: the problem may be one they do not show; the player's "
                          "description says what to look for\n");
    }
    if (!have_frame) {
        bprintf(&summary, "\nNo screenshot: the core's own picture was on the screen.\n");
    }

    // the zip
    z.dos_time = (unsigned)(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2);
    z.dos_date = (unsigned)((tm.tm_year > 80 ? tm.tm_year - 80 : 0) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);
    zip_add(&z, "summary.txt", summary.p, summary.n);
    zip_add(&z, "pacing.csv", csv.p, csv.n);
    zip_add(&z, "setup.txt", setup.p, setup.n);
    zip_add(&z, "osd.txt", osd.p, osd.n);
    zip_add(&z, "system.txt", sys.p, sys.n);
    zip_add(&z, "log.txt", log.p, log.n);
    pthread_mutex_lock(&lock);
    for (i = 0; i < EXTRA_MAX; i++) {
        if (extra[i].text != NULL) {
            char file[64];

            snprintf(file, sizeof file, "game/%.48s", extra[i].name);
            zip_add(&z, file, extra[i].text, strlen(extra[i].text));
        }
    }
    pthread_mutex_unlock(&lock);
    if (have_frame) {
        make_png(&png, pixels, width, height, format, palette);
        zip_add(&z, "screenshot.png", png.p, png.n);
    }
    zip_end(&z);

    // games/dnk/diagnostics/<Engine>_<YYYYMMDD>_<HHMMSS>.zip, written as
    // .part and renamed, so a half-written report is never taken for one
    snprintf(path, sizeof path, "%s/games", root());
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/games/dnk", root());
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/games/dnk/diagnostics", root());
    mkdir(path, 0755);
    snprintf(name, sizeof name, "%s_%04d%02d%02d_%02d%02d%02d", core, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    snprintf(path, sizeof path, "%s/games/dnk/diagnostics/%s.zip", root(), name);
    for (i = 2; stat(path, &st) == 0 && i < 100; i++) {
        snprintf(path, sizeof path, "%s/games/dnk/diagnostics/%s_%d.zip", root(), name, i);
    }
    snprintf(tmp, sizeof tmp, "%s.part", path);
    ok = 0;
    f = fopen(tmp, "wb");
    if (f != NULL) {
        ok = fwrite(z.data.p, 1, z.data.n, f) == z.data.n;
        ok = fflush(f) == 0 && ok;
        ok = fsync(fileno(f)) == 0 && ok;
        ok = fclose(f) == 0 && ok;
        if (ok) {
            ok = rename(tmp, path) == 0;
        }
        if (!ok) {
            unlink(tmp);
        }
    }
    if (ok) {
        fprintf(stderr, "mister: diagnostic report saved: %s (%zu bytes)\n", path, z.data.n);
    } else {
        fprintf(stderr, "mister: diagnostic report not saved: %s: %s\n", path, strerror(errno));
    }

    free(pixels);
    free(summary.p);
    free(csv.p);
    free(setup.p);
    free(osd.p);
    free(sys.p);
    free(log.p);
    free(png.p);
    free(z.data.p);
    free(z.central.p);
    return ok;
}

static void* thread_main(void* arg) {
    struct timespec next;
    int last_toggle = -1, tick = 0;

    (void)arg;
    MH_PinThreadToCPU1();
    setpriority(PRIO_PROCESS, 0, 10);
    sample(1);
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!stopping) {
        next.tv_nsec += 100000000;
        if (next.tv_nsec >= 1000000000) {
            next.tv_nsec -= 1000000000;
            next.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        if (stopping || !MH_IsOpen()) {
            continue;
        }
        if (++tick == 10) {
            tick = 0;
            sample(0);
        }
        // the OSD line flips bit 16 (host version 16)
        if (MH_HostVersion() >= 16) {
            int toggle = (int)((MH_OSDStatus() >> 16) & 1);

            if (last_toggle >= 0 && toggle != last_toggle) {
                MH_OSDInfo(write_report() ? MH_INFO_DIAG_SAVED : MH_INFO_DIAG_FAILED);
                // the report took a while: the record goes on from now
                clock_gettime(CLOCK_MONOTONIC, &next);
                sample(1);
                tick = 0;
            }
            last_toggle = toggle;
        }
    }
    return NULL;
}

void mh_diag_start(void) {
    // MISTER_HYBRID_DIAG=0: no record and no report
    if (running || (getenv("MISTER_HYBRID_DIAG") && atoi(getenv("MISTER_HYBRID_DIAG")) == 0)) {
        return;
    }
    memset(record, 0, sizeof record);
    record_next = 0;
    stopping = 0;
    running = pthread_create(&thread, NULL, thread_main, NULL) == 0;
}

void mh_diag_stop(void) {
    if (!running) {
        return;
    }
    stopping = 1;
    pthread_join(thread, NULL);
    running = 0;
}

void MH_DiagAdd(const char* name, const char* text) {
    int i, free_slot = -1;

    if (name == NULL || name[0] == 0 || strchr(name, '/') != NULL) {
        return;
    }
    pthread_mutex_lock(&lock);
    for (i = 0; i < EXTRA_MAX; i++) {
        if (extra[i].text != NULL && strcmp(extra[i].name, name) == 0) {
            break;
        }
        if (extra[i].text == NULL && free_slot < 0) {
            free_slot = i;
        }
    }
    if (i == EXTRA_MAX) {
        i = free_slot;
    }
    if (i >= 0) {
        free(extra[i].text);
        extra[i].text = text != NULL ? strdup(text) : NULL;
        snprintf(extra[i].name, sizeof extra[i].name, "%s", name);
    }
    pthread_mutex_unlock(&lock);
}
