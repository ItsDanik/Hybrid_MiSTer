// SPDX-License-Identifier: GPL-2.0-or-later
// Part of Hybrid_MiSTer (hps/LICENSE).
//
// Resolves physical gamepad buttons to core buttons through Main_MiSTer's
// mapping files, for MH_MenuButtons() (mister_hybrid.h).
//
// Main_MiSTer refuses to map one physical button to two core buttons, so a
// button with a game function cannot also be the core's "confirm" button.
// Menus confirm and go back with the MiSTer menu's own OK / Back instead, and
// this module finds the core buttons they drive by reading the controller's
// menu mapping (input_<vid>_<pid>_v3.map: its Menu OK / Menu Back, else its
// A / B, as Main_MiSTer does) and its mapping for this core
// (<core>_input_<vid>_<pid>_v3.map, or the core's default "jn" mapping when
// there is none). A controller without a menu mapping gets the buttons its
// own console confirms and goes back with (convention_codes()). Started from
// a menu entry with a set name (an MGL), Main names the mapping after that
// (<set>_input_...): MH_MappingName(). Up to host version 13 the OSD had
// options for other buttons (status bits 16..23); they are gone.

#include "mister_hybrid.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef CONFIG_DIR
#define CONFIG_DIR "/media/fat/config/"
#endif
#define INPUTS_DIR CONFIG_DIR "inputs/"
#define RESCAN_SECONDS 2

// Main_MiSTer map layout (input.h): uint32 per entry, low 16 bits = Linux input
// code, high 16 bits = alternative button
#define MAP_ENTRIES 32
#define SYS_BTN_A 4 // A, B, X, Y, L, R, Select, Start follow
#define SYS_BTN_B 5
#define SYS_BTN_MENU_FUNC 23 // low 16 bits = menu OK, high = menu Back
#define NUM_NAMED 8

// core buttons: bit 4.. in the order of the core's "J1," list
#define CORE_FIRST 4
#define CORE_LAST 31

// Default mapping of the core buttons to named buttons (index into A, B, X, Y,
// L, R, Select, Start, or -1), the core's "jn," list from MISTER_HYBRID_JN
static int core_default[CORE_LAST - CORE_FIRST + 1];
static int core_default_loaded;

static void load_core_default(void) {
    static const char* const names[NUM_NAMED] = { "A", "B", "X", "Y", "L", "R", "Select", "Start" };
    const char* jn = getenv("MISTER_HYBRID_JN");
    int i, k;

    for (i = 0; i <= CORE_LAST - CORE_FIRST; i++) {
        core_default[i] = -1;
    }
    if (jn == NULL || jn[0] == 0) {
        jn = "A,B,X,Y,L,R,Select,Start";
    }
    for (i = 0; i <= CORE_LAST - CORE_FIRST && *jn; i++) {
        size_t len = strcspn(jn, ",");
        for (k = 0; k < NUM_NAMED; k++) {
            if (strlen(names[k]) == len && strncmp(jn, names[k], len) == 0) {
                core_default[i] = k;
            }
        }
        jn += len;
        if (*jn == ',') {
            jn++;
        }
    }
    core_default_loaded = 1;
}

#define MAX_DEVICES 8

typedef struct {
    unsigned vid, pid;
    int virt; // made by a program (Zaparoo, a launcher), not plugged in
} tDevice;

// Linux gamepad codes of A, B, X, Y, L, R, Select, Start (MiSTer's names are
// SNES positions: A east, B south, X north, Y west), used when a controller
// has no menu mapping file
static const uint16_t standard_codes[NUM_NAMED] = { 0x131, 0x130, 0x133, 0x134, 0x136, 0x137, 0x13a, 0x13b };
#define CODE_SOUTH 0x130
#define CODE_EAST 0x131
#define VENDOR_NINTENDO 0x057e

// Menu OK / Menu Back (SYS_BTN_MENU_FUNC: OK low, Back high) of a controller
// without a menu mapping, after its console: on Nintendo's the right button
// (A) confirms and the bottom one (B) goes back, as in the MiSTer menu; on
// every other (PlayStation, Xbox and most others) the bottom one (Cross, A)
// confirms and the right one (Circle, B) goes back. Linux names the buttons
// by place (BTN_SOUTH is the bottom one), whatever is printed on them. 8BitDo
// pads in their Switch mode are Nintendo's here.
static uint32_t convention_codes(const tDevice* dev) {
    return dev->vid == VENDOR_NINTENDO ? CODE_EAST | CODE_SOUTH << 16 : CODE_SOUTH | CODE_EAST << 16;
}

static time_t last_scan;
static uint32_t ok_result, back_result;

// What the last resolve() was made from. Reading the mapping files every two
// seconds (and, for a controller without one, the whole of config/inputs/)
// stopped the game for 10 to 20ms on the SD card each time: it is only done
// again when the controllers, the folders or the files it read have changed.
// Main_MiSTer writes a mapping file in place ("Define buttons"), which
// changes the file's time; a new one changes the folder's.
#define MAX_READ 32
static tDevice seen_devices[MAX_DEVICES];
static int seen_count = -1;
static struct {
    char path[384];
    time_t mtime;
} files_read[MAX_READ];
static int files_count;
static time_t dir_mtime[2];

static time_t mtime_of(const char* path) {
    struct stat st;

    return stat(path, &st) == 0 ? st.st_mtime : (time_t)-1;
}

static void note_file(const char* path) {
    if (files_count < MAX_READ) {
        snprintf(files_read[files_count].path, sizeof(files_read[0].path), "%s", path);
        files_read[files_count].mtime = mtime_of(path);
        files_count++;
    }
}

// 1 if what resolve() was made from is no longer what there is
static int inputs_changed(const tDevice* devices, int count) {
    int i;

    if (count != seen_count || memcmp(devices, seen_devices, count * sizeof(tDevice)) != 0) {
        return 1;
    }
    if (mtime_of(INPUTS_DIR) != dir_mtime[0] || mtime_of(CONFIG_DIR) != dir_mtime[1]) {
        return 1;
    }
    for (i = 0; i < files_count; i++) {
        if (mtime_of(files_read[i].path) != files_read[i].mtime) {
            return 1;
        }
    }
    return 0;
}

// 1 if a "B: KEY=" / "B: ABS=" bitmap of /proc/bus/input/devices (longs in
// hex, the highest first) has one of the codes first..last
static int has_codes(const char* bits, int first, int last) {
    unsigned long words[16];
    int n = 0, k;

    while (n < 16 && *bits) {
        char* end;
        words[n] = strtoul(bits, &end, 16);
        if (end == bits) {
            break;
        }
        n++;
        bits = end;
    }
    for (k = first; k <= last; k++) {
        int w = k / (8 * (int)sizeof(long));
        if (w < n && (words[n - 1 - w] >> (k % (8 * sizeof(long)))) & 1) {
            return 1;
        }
    }
    return 0;
}

// Connected joysticks from /proc/bus/input/devices: entries with a js handler,
// or a stick or hat and a gamepad's buttons (BTN_JOYSTICK..BTN_DIGI - 1; a
// keyboard's "Consumer Control" has some of those, and ABS_VOLUME). A Switch
// Pro Controller of hid-nintendo has no js handler on the MiSTer: only
// Zaparoo's virtual gamepad was found, its menu mapping without buttons, and
// Menu OK / Back were no button at all (CrispyDoom issue #2, 2026-10-22).
static int list_joysticks(tDevice* devices, int max) {
    FILE* f = fopen("/proc/bus/input/devices", "r");
    char line[512];
    unsigned vid = 0, pid = 0;
    int virt = 0, buttons = 0, js = 0, axes = 0;
    int count = 0;
    int i, dup, end;

    if (f == NULL) {
        return 0;
    }
    do {
        end = fgets(line, sizeof(line), f) == NULL;
        if (end || line[0] == '\n') {
            // the end of an entry
            dup = 0;
            for (i = 0; i < count; i++) {
                dup |= devices[i].vid == vid && devices[i].pid == pid;
            }
            if ((js || (buttons && axes)) && !dup && count < max) {
                memset(&devices[count], 0, sizeof(devices[count]));
                devices[count].vid = vid;
                devices[count].pid = pid;
                devices[count].virt = virt;
                count++;
            }
            buttons = js = axes = 0;
        } else if (strncmp(line, "I:", 2) == 0) {
            char* v = strstr(line, "Vendor=");
            char* p = strstr(line, "Product=");
            vid = v ? (unsigned)strtoul(v + 7, NULL, 16) : 0;
            pid = p ? (unsigned)strtoul(p + 8, NULL, 16) : 0;
            virt = 0;
        } else if (strncmp(line, "S: Sysfs=/devices/virtual/", 26) == 0) {
            virt = 1;
        } else if (strncmp(line, "H:", 2) == 0 && (strstr(line, "=js") != NULL || strstr(line, " js") != NULL)) {
            js = 1;
        } else if (strncmp(line, "B: KEY=", 7) == 0) {
            buttons = has_codes(line + 7, 0x120, 0x13f);
        } else if (strncmp(line, "B: ABS=", 7) == 0) {
            // ABS_X..ABS_RZ, ABS_HAT0X / ABS_HAT0Y
            axes = has_codes(line + 7, 0x00, 0x05) || has_codes(line + 7, 0x10, 0x11);
        }
    } while (!end);
    fclose(f);
    return count;
}

static int read_map(const char* path, uint32_t map[MAP_ENTRIES]) {
    FILE* f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        return 0;
    }
    note_file(path);
    memset(map, 0, MAP_ENTRIES * sizeof(uint32_t));
    n = fread(map, 1, MAP_ENTRIES * sizeof(uint32_t), f);
    fclose(f);
    return n >= (SYS_BTN_MENU_FUNC + 1) * sizeof(uint32_t);
}

// Loads <prefix>input_<vid>_<pid>[_<variant>]_v3.map like Main_MiSTer: from
// config/inputs/ or, for old setups, config/
static int load_map(const char* prefix, const tDevice* dev, uint32_t map[MAP_ENTRIES]) {
    char name[128], path[384];
    size_t len;
    DIR* dir;
    struct dirent* ent;
    int found = 0;

    snprintf(name, sizeof(name), "%sinput_%04x_%04x_v3.map", prefix, dev->vid, dev->pid);
    snprintf(path, sizeof(path), INPUTS_DIR "%s", name);
    if (read_map(path, map)) {
        return 1;
    }
    snprintf(path, sizeof(path), CONFIG_DIR "%s", name);
    if (read_map(path, map)) {
        return 1;
    }
    // unique mapping or name hash variants: <id>_<suffix>_v3.map, but not the
    // "_m" modifier maps
    snprintf(name, sizeof(name), "%sinput_%04x_%04x_", prefix, dev->vid, dev->pid);
    len = strlen(name);
    dir = opendir(INPUTS_DIR);
    if (dir == NULL) {
        return 0;
    }
    while (!found && (ent = readdir(dir)) != NULL) {
        size_t n = strlen(ent->d_name);
        if (strncmp(ent->d_name, name, len) == 0 && n > len + 7 && strcmp(ent->d_name + n - 7, "_v3.map") == 0
            && !(n >= 9 && strcmp(ent->d_name + n - 9, "_m_v3.map") == 0)) {
            snprintf(path, sizeof(path), INPUTS_DIR "%.255s", ent->d_name);
            found = read_map(path, map);
        }
    }
    closedir(dir);
    return found;
}

// Core buttons driven by the physical button with Linux code `code`
static uint32_t core_bits_for_code(const uint32_t core[MAP_ENTRIES], uint32_t code) {
    uint32_t bits = 0;
    int i;

    if (code == 0) {
        return 0;
    }
    for (i = CORE_FIRST; i <= CORE_LAST; i++) {
        if ((core[i] & 0xFFFF) == code || (core[i] >> 16) == code) {
            bits |= 1u << i;
        }
    }
    return bits;
}

// The menu's OK or Back button, A or B when not set (as Main_MiSTer does)
static uint32_t menu_code(const uint32_t menu[MAP_ENTRIES], int back) {
    uint32_t func = back ? menu[SYS_BTN_MENU_FUNC] >> 16 : menu[SYS_BTN_MENU_FUNC] & 0xFFFF;

    return func ? func : menu[back ? SYS_BTN_B : SYS_BTN_A] & 0xFFFF;
}

// the Linux codes of the physical OK and Back buttons (of the controllers in
// use): for MH_MenuButtonsPressed(), which also reads them from Linux
static uint32_t ok_code, back_code;
int mh_pad_key_down(uint32_t code); // mister_triggers.c
int mh_osd_hold(int* held, int pressed); // mister_hybrid.c

static void resolve(void) {
    tDevice devices[MAX_DEVICES];
    uint32_t menu[MAX_DEVICES][MAP_ENTRIES];
    uint32_t core[MAX_DEVICES][MAP_ENTRIES];
    int score[MAX_DEVICES];
    int count, best = 0;
    char prefix[80];
    int i, k;

    if (!core_default_loaded) {
        load_core_default();
    }
    count = list_joysticks(devices, MAX_DEVICES);
    if (!inputs_changed(devices, count)) {
        return;
    }
    memset(seen_devices, 0, sizeof(seen_devices));
    memcpy(seen_devices, devices, count * sizeof(tDevice));
    seen_count = count;
    dir_mtime[0] = mtime_of(INPUTS_DIR);
    dir_mtime[1] = mtime_of(CONFIG_DIR);
    files_count = 0;
    snprintf(prefix, sizeof(prefix), "%s_", MH_MappingName());
    ok_result = 0;
    back_result = 0;
    ok_code = 0;
    back_code = 0;
    for (i = 0; i < count; i++) {
        int has_menu = load_map("", &devices[i], menu[i]);
        int has_core = load_map(prefix, &devices[i], core[i]);

        // a menu mapping without OK and Back, nor A and B (Zaparoo's has
        // its axes alone) is none
        if (has_menu && menu_code(menu[i], 0) == 0 && menu_code(menu[i], 1) == 0) {
            has_menu = 0;
        }
        if (!has_menu) {
            memset(menu[i], 0, sizeof(menu[i]));
            for (k = 0; k < NUM_NAMED; k++) {
                menu[i][SYS_BTN_A + k] = standard_codes[k];
            }
            menu[i][SYS_BTN_MENU_FUNC] = convention_codes(&devices[i]);
        }
        if (!has_core) {
            // what Main_MiSTer derives from the core's default ("jn") mapping
            memset(core[i], 0, sizeof(core[i]));
            for (k = CORE_FIRST; k <= CORE_LAST; k++) {
                if (core_default[k - CORE_FIRST] >= 0) {
                    core[i][k] = menu[i][SYS_BTN_A + core_default[k - CORE_FIRST]];
                }
            }
        }
        // Controllers set up for this core or the menu are the ones in use,
        // and of those, or of none set up, the ones plugged in, not the
        // virtual joysticks of other tools (Zaparoo has a gamepad's buttons):
        // without a mapping each has its own buttons (convention_codes()), and
        // those of all the controllers taken are put together
        score[i] = has_core * 4 + has_menu * 2 + !devices[i].virt;
        if (score[i] > best) {
            best = score[i];
        }
    }
    if (count == 0) {
        // No controller to look up (keyboard as joystick, a device that is not
        // listed): assume the default mapping, OK on A and Back on B
        for (k = CORE_FIRST; k <= CORE_LAST; k++) {
            ok_result |= core_default[k - CORE_FIRST] == 0 ? 1u << k : 0;
            back_result |= core_default[k - CORE_FIRST] == 1 ? 1u << k : 0;
        }
    }
    for (i = 0; i < count; i++) {
        if (score[i] == best) {
            ok_result |= core_bits_for_code(core[i], menu_code(menu[i], 0));
            back_result |= core_bits_for_code(core[i], menu_code(menu[i], 1));
            if (ok_code == 0) {
                ok_code = menu_code(menu[i], 0);
                back_code = menu_code(menu[i], 1);
                printf("mister: menu buttons of %04x:%04x%s: OK 0x%x, Back 0x%x\n", devices[i].vid, devices[i].pid,
                    score[i] & 2 ? "" : " (no mapping)", ok_code, back_code);
            }
        }
    }
}

void MH_MenuButtons(uint64_t osd_status, uint32_t* ok_bits, uint32_t* back_bits) {
    time_t now = time(NULL);

    (void)osd_status;
    if (seen_count < 0 || now - last_scan >= RESCAN_SECONDS) {
        last_scan = now;
        resolve();
    }
    *ok_bits = ok_result;
    *back_bits = back_result;
}

void MH_MenuButtonsPressed(uint64_t osd_status, uint32_t joystick, int* ok, int* back) {
    static int ok_held, back_held;
    uint32_t ok_bits, back_bits;

    MH_MenuButtons(osd_status, &ok_bits, &back_bits);
    // through a core button the player gave the physical one, or the physical
    // button itself: one without a function in this core is passed to no
    // core button at all. Main_MiSTer holds back the core's buttons while its
    // OSD is open, Linux does not: mh_osd_hold().
    *ok = mh_osd_hold(&ok_held, (joystick & ok_bits) != 0 || mh_pad_key_down(ok_code));
    *back = mh_osd_hold(&back_held, (joystick & back_bits) != 0 || mh_pad_key_down(back_code));
}
