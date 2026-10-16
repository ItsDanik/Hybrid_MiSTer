// Analog triggers (L2 / R2), the stick clicks (L3 / R3) and rumble, for
// MH_ReadTriggers(), MH_ReadStickButtons() and MH_Rumble() (mister_hybrid.h):
// what a gamepad has that MiSTer does not pass on.
//
// Main_MiSTer gives a core the two sticks and the buttons of a gamepad, but
// not its triggers as axes. The game runs on the same Linux, so this module
// asks the kernel for them: every /dev/input/event* that is a gamepad is kept
// open and its trigger axes are read with EVIOCGABS -- a question about the
// device's state, which is answered whether or not Main_MiSTer has grabbed
// the device for itself (a grab only stops the events from being delivered).
//
// Which two axes are the triggers differs between controllers and drivers:
// BRAKE / GAS, Z / RZ (Xbox, PlayStation), RX / RY (some generic ones, where
// Z / RZ are the right stick) or HAT2Y / HAT2X. A stick rests in the middle
// of its range and a trigger at the bottom of it, so the first of those pairs
// seen with both axes at the bottom is taken. A controller is looked at until
// that happens; one without such a pair has no triggers here.
//
// Main_MiSTer decides which controller is which player, and that cannot be
// seen from here: the values are the furthest-pressed of all controllers.
//
// Rumble is Linux force feedback on the same devices: one effect a
// controller, stored again and started whenever the strength changes. It is
// given two seconds to run, so that the motors of a game that has gone away
// stop by themselves; a game that wants them to go on keeps calling
// MH_Rumble(), which stores the effect again every second. Every controller
// that can rumble does.

#include "mister_hybrid.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef INPUT_DIR
#define INPUT_DIR "/dev/input"
#endif
#define MAX_PADS 8
#define RESCAN_SECONDS 2

typedef struct {
    int fd; // -1: free
    char name[256];
    int seen;        // in the directory at the last scan
    int left, right; // axis codes, -1 until they are known
    struct input_absinfo left_info, right_info;
    int rumbles; // it can, and was opened for writing
    int effect;  // the stored effect, -1 = none yet
} pad;

static pad pads[MAX_PADS];
static int pads_ready;
static time_t scanned;

// Devices that were opened and are no gamepads: not opened again while the
// node is the same one. Opening an event device takes the kernel up to ten
// milliseconds (a Bluetooth one more), and with a keyboard, a mouse and what
// else a MiSTer has that was a game standing still for 50ms at every scan.
#define MAX_OTHERS 32
typedef struct {
    char name[32]; // "" = free
    ino_t ino;
    time_t ctime;
    int seen;
} other;

static other others[MAX_OTHERS];

// 1: this node was looked at before and is no gamepad
static int known_other(const char* name, const struct stat* st) {
    int i;

    for (i = 0; i < MAX_OTHERS; i++) {
        if (others[i].name[0] && strcmp(others[i].name, name) == 0) {
            if (others[i].ino == st->st_ino && others[i].ctime == st->st_ctime) {
                others[i].seen = 1;
                return 1;
            }
            others[i].name[0] = 0; // another device has the name now
        }
    }
    return 0;
}

static void add_other(const char* name, const struct stat* st) {
    int i;

    for (i = 0; i < MAX_OTHERS; i++) {
        if (!others[i].name[0] && strlen(name) < sizeof others[i].name) {
            strcpy(others[i].name, name);
            others[i].ino = st->st_ino;
            others[i].ctime = st->st_ctime;
            others[i].seen = 1;
            return;
        }
    }
}

static const int pairs[][2] = {
    { ABS_BRAKE, ABS_GAS },
    { ABS_Z, ABS_RZ },
    { ABS_RX, ABS_RY },
    { ABS_HAT2Y, ABS_HAT2X },
};

static int has_bit(const unsigned long* bits, int n) {
    return (bits[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
}

// A gamepad or joystick: it has buttons of one, and axes
static int is_gamepad(int fd) {
    unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1];
    unsigned long ev[EV_MAX / (8 * sizeof(long)) + 1];
    int k;

    memset(keys, 0, sizeof keys);
    memset(ev, 0, sizeof ev);
    if (ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) < 0 || !has_bit(ev, EV_ABS) || !has_bit(ev, EV_KEY)) {
        return 0;
    }
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0) {
        return 0;
    }
    for (k = BTN_JOYSTICK; k < BTN_DIGI; k++) {
        if (has_bit(keys, k)) {
            return 1;
        }
    }
    return 0;
}

static void scan(void) {
    DIR* dir = opendir(INPUT_DIR);
    struct dirent* e;
    int i;

    for (i = 0; i < MAX_PADS; i++) {
        pads[i].seen = 0;
    }
    for (i = 0; i < MAX_OTHERS; i++) {
        others[i].seen = 0;
    }
    while (dir != NULL && (e = readdir(dir)) != NULL) {
        unsigned long ff[FF_MAX / (8 * sizeof(long)) + 1];
        struct stat st;
        char path[300];
        int fd, writable, free_slot = -1;

        if (strncmp(e->d_name, "event", 5) != 0) {
            continue;
        }
        for (i = 0; i < MAX_PADS; i++) {
            if (pads[i].fd >= 0 && strcmp(pads[i].name, e->d_name) == 0) {
                pads[i].seen = 1;
                break;
            }
            if (pads[i].fd < 0 && free_slot < 0) {
                free_slot = i;
            }
        }
        if (i < MAX_PADS || free_slot < 0) {
            continue;
        }
        snprintf(path, sizeof path, INPUT_DIR "/%s", e->d_name);
        if (stat(path, &st) < 0 || known_other(e->d_name, &st)) {
            continue;
        }
        // (for writing too: force feedback is written to the device)
        writable = 1;
        fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            writable = 0;
            fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        }
        if (fd < 0) {
            continue;
        }
        if (!is_gamepad(fd)) {
            close(fd);
            add_other(e->d_name, &st);
            continue;
        }
        memset(ff, 0, sizeof ff);
        pads[free_slot].rumbles = writable && ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff), ff) >= 0 && has_bit(ff, FF_RUMBLE);
        pads[free_slot].effect = -1;
        pads[free_slot].fd = fd;
        snprintf(pads[free_slot].name, sizeof pads[free_slot].name, "%s", e->d_name);
        pads[free_slot].seen = 1;
        pads[free_slot].left = pads[free_slot].right = -1;
    }
    if (dir != NULL) {
        closedir(dir);
    }
    for (i = 0; i < MAX_PADS; i++) {
        if (pads[i].fd >= 0 && !pads[i].seen) {
            close(pads[i].fd);
            pads[i].fd = -1;
        }
    }
    for (i = 0; i < MAX_OTHERS; i++) {
        if (!others[i].seen) {
            others[i].name[0] = 0; // unplugged
        }
    }
}

static int at_rest(const struct input_absinfo* a) {
    int range = a->maximum - a->minimum;

    return range >= 15 && a->value <= a->minimum + range / 8;
}

// Find the pad's trigger axes; 0 if they are not known (yet)
static int find_axes(pad* p) {
    unsigned long abs_bits[ABS_MAX / (8 * sizeof(long)) + 1];
    size_t k;

    memset(abs_bits, 0, sizeof abs_bits);
    if (ioctl(p->fd, EVIOCGBIT(EV_ABS, sizeof abs_bits), abs_bits) < 0) {
        return 0;
    }
    for (k = 0; k < sizeof pairs / sizeof pairs[0]; k++) {
        struct input_absinfo l, r;

        if (!has_bit(abs_bits, pairs[k][0]) || !has_bit(abs_bits, pairs[k][1])) {
            continue;
        }
        if (ioctl(p->fd, EVIOCGABS(pairs[k][0]), &l) < 0 || ioctl(p->fd, EVIOCGABS(pairs[k][1]), &r) < 0) {
            continue;
        }
        if (at_rest(&l) && at_rest(&r)) {
            p->left = pairs[k][0];
            p->right = pairs[k][1];
            return 1;
        }
    }
    return 0;
}

static int scaled(const struct input_absinfo* a) {
    int range = a->maximum - a->minimum;
    int v = range > 0 ? (a->value - a->minimum) * 255 / range : 0;

    return v < 0 ? 0 : v > 255 ? 255 : v;
}

// The list of controllers, looked over again every two seconds
static void refresh(void) {
    time_t now = time(NULL);
    int i;

    if (!pads_ready) {
        for (i = 0; i < MAX_PADS; i++) {
            pads[i].fd = -1;
        }
        pads_ready = 1;
        scanned = 0;
    }
    if (now - scanned >= RESCAN_SECONDS || now < scanned) {
        scanned = now;
        scan();
    }
}

static long long now_ms(void) {
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int MH_Rumble(int strong, int weak) {
    static int last_strong, last_weak;
    static long long sent;
    long long now = now_ms();
    int any = 0;
    int i;

    strong = strong < 0 ? 0 : strong > 255 ? 255 : strong;
    weak = weak < 0 ? 0 : weak > 255 ? 255 : weak;
    refresh();
    if (strong == last_strong && weak == last_weak) {
        // nothing new: only kept going
        if ((strong == 0 && weak == 0) || now - sent < 1000) {
            for (i = 0; i < MAX_PADS; i++) {
                any |= pads[i].fd >= 0 && pads[i].rumbles;
            }
            return any;
        }
    } else if ((strong != 0 || weak != 0) && now - sent < 25) {
        // a controller is not told more than forty times a second
        return 1;
    }
    for (i = 0; i < MAX_PADS; i++) {
        pad* p = &pads[i];
        struct input_event ev;
        struct ff_effect effect;

        if (p->fd < 0 || !p->rumbles) {
            continue;
        }
        any = 1;
        memset(&ev, 0, sizeof ev);
        ev.type = EV_FF;
        if (strong == 0 && weak == 0) {
            if (p->effect >= 0) {
                ev.code = (unsigned short)p->effect;
                ev.value = 0;
                if (write(p->fd, &ev, sizeof ev) < 0 && errno == ENODEV) {
                    close(p->fd);
                    p->fd = -1;
                }
            }
            continue;
        }
        memset(&effect, 0, sizeof effect);
        effect.type = FF_RUMBLE;
        effect.id = (short)p->effect;
        effect.u.rumble.strong_magnitude = (unsigned short)(strong * 257);
        effect.u.rumble.weak_magnitude = (unsigned short)(weak * 257);
        effect.replay.length = 2000;
        if (ioctl(p->fd, EVIOCSFF, &effect) < 0) {
            if (errno == ENODEV) {
                close(p->fd);
                p->fd = -1;
            }
            continue;
        }
        p->effect = effect.id;
        ev.code = (unsigned short)p->effect;
        ev.value = 1;
        if (write(p->fd, &ev, sizeof ev) < 0) {
            continue;
        }
    }
    last_strong = strong;
    last_weak = weak;
    sent = now;
    return any;
}

// 1 while the button with Linux code `code` (BTN_*) is pressed on any
// controller, read from Linux as the stick buttons are (mister_joymap.c)
int mh_pad_key_down(uint32_t code) {
    unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1];
    int down = 0;
    int i;

    if (code == 0 || code >= KEY_MAX) {
        return 0;
    }
    refresh();
    for (i = 0; i < MAX_PADS && !down; i++) {
        pad* p = &pads[i];

        if (p->fd < 0) {
            continue;
        }
        memset(keys, 0, sizeof keys);
        if (ioctl(p->fd, EVIOCGKEY(sizeof keys), keys) < 0) {
            continue;
        }
        down = has_bit(keys, code);
    }
    return down;
}

int MH_ReadStickButtons(int* left, int* right) {
    const char* fixed = getenv("MISTER_HYBRID_STICKBUTTONS"); // "left,right": for tests without a controller
    unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1];
    int found = 0;
    int i;

    *left = *right = 0;
    if (fixed != NULL) {
        if (sscanf(fixed, "%d,%d", left, right) < 2) {
            *left = *right = 0;
        }
        return 1;
    }
    refresh();
    for (i = 0; i < MAX_PADS; i++) {
        pad* p = &pads[i];

        if (p->fd < 0) {
            continue;
        }
        memset(keys, 0, sizeof keys);
        if (ioctl(p->fd, EVIOCGKEY(sizeof keys), keys) < 0) {
            if (errno == ENODEV) { // unplugged
                close(p->fd);
                p->fd = -1;
            }
            continue;
        }
        found = 1;
        *left |= has_bit(keys, BTN_THUMBL);
        *right |= has_bit(keys, BTN_THUMBR);
    }
    return found;
}

int MH_ReadTriggers(int* left, int* right) {
    const char* fixed = getenv("MISTER_HYBRID_TRIGGERS"); // "left,right": for tests without a controller
    int l = 0, r = 0, found = 0;
    int i;

    if (fixed != NULL) {
        if (sscanf(fixed, "%d,%d", &l, &r) < 2) {
            l = r = 0;
        }
        *left = l;
        *right = r;
        return 1;
    }
    refresh();
    for (i = 0; i < MAX_PADS; i++) {
        pad* p = &pads[i];
        int v;

        if (p->fd < 0 || (p->left < 0 && !find_axes(p))) {
            continue;
        }
        if (ioctl(p->fd, EVIOCGABS(p->left), &p->left_info) < 0 || ioctl(p->fd, EVIOCGABS(p->right), &p->right_info) < 0) {
            if (errno == ENODEV) { // unplugged
                close(p->fd);
                p->fd = -1;
            }
            continue;
        }
        found = 1;
        v = scaled(&p->left_info);
        if (v > l) {
            l = v;
        }
        v = scaled(&p->right_info);
        if (v > r) {
            r = v;
        }
    }
    *left = l;
    *right = r;
    return found;
}
