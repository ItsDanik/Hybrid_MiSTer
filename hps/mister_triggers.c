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
// that happens; one without such a pair has no triggers here. Z / RZ and
// RX / RY are triggers only on a controller that has both pairs, and only
// while the other pair, its right stick, is in the middle: a stick pushed up
// and to the left is at the bottom of both its axes too. A Switch Pro
// Controller has RX / RY alone, its right stick, and its ZL / ZR are buttons:
// once that stick had been pushed up and left, it was taken for the triggers,
// which then held fire and run while it was let go.
//
// Main_MiSTer decides which controller is which player, and that cannot be
// seen from here: the values are the furthest-pressed of all controllers.
//
// RACING WHEELS: Main_MiSTer knows a list of wheels (input.cpp, setup_wheels:
// Logitech, Fanatec, Thrustmaster and others) and hands a core their pedals
// itself, whatever the model's axes are and whichever way they run: the gas on
// the left stick's up-down axis and the brake on the right stick's, 0 let go to
// -127 pressed (and the wheel on the left stick's across). So for a wheel of
// that list the pedals are taken from the core's sticks, as the triggers: gas
// the right one, brake the left; its own axes are not looked at. The list is
// Main_MiSTer's (2026-10-16), by vendor and product. While such a wheel is
// connected, a gamepad's sticks pushed up count as pedals too, and the SDL
// driver's joystick 1 is both players' controllers (MH_WheelConnected()), so
// that the wheel steers whichever player Main_MiSTer made it.
//
// Rumble is Linux force feedback on the same devices: one effect a
// controller, stored again and started whenever the strength changes. It is
// given two seconds to run, so that the motors of a game that has gone away
// stop by themselves; a game that wants them to go on keeps calling
// MH_Rumble(), which stores the effect again every second. Every controller
// that can rumble does.
//
// FORCES (MH_Force*: a racing wheel's, or a joystick's): the same devices,
// those with FF_CONSTANT. Each slot an effect stored on every such device,
// played until it ends; one with no end is stored with two seconds to run and
// stored again by MH_ForceKeep() every second, for the same reason as the
// rumble. Main_MiSTer may have grabbed the wheel: force feedback is written
// to it all the same (a grab stops only the events read from it).

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
    int wheel;   // a wheel Main_MiSTer hands the pedals of (is_wheel)
    int rumbles; // it can, and was opened for writing
    int effect;  // the stored effect, -1 = none yet
    int forces;  // FF_CONSTANT, and opened for writing (MH_Force*)
    int force_id[MH_FORCE_SLOTS]; // the slots' stored effects, -1 = none yet
    int sines;   // FF_PERIODIC with FF_SINE as well
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

// One of the wheels whose pedals Main_MiSTer hands on (input.cpp, setup_wheels)
static int is_wheel(int fd) {
    struct input_id id;
    char name[256] = "";

    if (ioctl(fd, EVIOCGID, &id) < 0) {
        return 0;
    }
    ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
    switch (id.vendor) {
    case 0x046d: // Logitech: G25, G27, G29, Driving Force, Driving Force Pro, Driving Force GT
        return id.product == 0xc299 || id.product == 0xc29b || id.product == 0xc24f || id.product == 0xc294 ||
               id.product == 0xc298 || id.product == 0xc29a;
    case 0x0eb7: // Fanatec: ClubSport V2.5, Podium DD1, DD2
        return id.product == 0x0004 || id.product == 0x0006 || id.product == 0x0007;
    case 0x06f8: // Guillemot (Thrustmaster): Force Feedback Racing Wheel
        return id.product == 0x0004;
    case 0x044f: // Thrustmaster: FGT Rumble 3-in-1, F430 Cockpit Wireless, T300RS
        return id.product == 0xb655 || id.product == 0xb65b || id.product == 0xb66e;
    case 0x2341: // a NeGcon through an Arduino / RetroZord
        return strstr(name, "RZordPsWheel") != NULL;
    case 0x1209:
        return id.product == 0x595a && strstr(name, "RZordPsWheel") != NULL;
    case 0x16d0: // ... or a Reflex Adapt
        return id.product == 0x127e && strstr(name, "ReflexPSWheel") != NULL;
    }
    return 0;
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
        pads[free_slot].forces = writable && has_bit(ff, FF_CONSTANT);
        pads[free_slot].sines = pads[free_slot].forces && has_bit(ff, FF_PERIODIC) && has_bit(ff, FF_SINE);
        for (i = 0; i < MH_FORCE_SLOTS; i++) {
            pads[free_slot].force_id[i] = -1;
        }
        if (pads[free_slot].forces && has_bit(ff, FF_GAIN)) {
            struct input_event gain;

            memset(&gain, 0, sizeof gain);
            gain.type = EV_FF;
            gain.code = FF_GAIN;
            gain.value = 0xffff;
            if (write(fd, &gain, sizeof gain) < 0) {
                // (no gain: as it is)
            }
        }
        pads[free_slot].fd = fd;
        snprintf(pads[free_slot].name, sizeof pads[free_slot].name, "%s", e->d_name);
        pads[free_slot].seen = 1;
        pads[free_slot].left = pads[free_slot].right = -1;
        pads[free_slot].wheel = is_wheel(fd);
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
        if (pairs[k][0] == ABS_Z || pairs[k][0] == ABS_RX) {
            // the right stick: the other of Z / RZ and RX / RY, in the middle
            const int stick_x = pairs[k][0] == ABS_Z ? ABS_RX : ABS_Z;
            const int stick_y = pairs[k][0] == ABS_Z ? ABS_RY : ABS_RZ;
            struct input_absinfo x, y;

            if (!has_bit(abs_bits, stick_x) || !has_bit(abs_bits, stick_y) ||
                ioctl(p->fd, EVIOCGABS(stick_x), &x) < 0 || ioctl(p->fd, EVIOCGABS(stick_y), &y) < 0 ||
                at_rest(&x) || at_rest(&y)) {
                continue;
            }
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

// ---- forces ----

typedef struct {
    int kind;         // 0 none, 1 constant, 2 sine
    int level;        // the constant's, or the sine's magnitude
    int period;       // the sine's, ms
    long long end;    // when it stops (0: no end)
    long long stored; // when it was last stored (no end: again every second)
} force;

static force forces[MH_FORCE_SLOTS];

static void force_stop_on(pad* p, int slot) {
    struct input_event ev;

    if (p->force_id[slot] < 0) {
        return;
    }
    memset(&ev, 0, sizeof ev);
    ev.type = EV_FF;
    ev.code = (unsigned short)p->force_id[slot];
    ev.value = 0;
    if (write(p->fd, &ev, sizeof ev) < 0 && errno == ENODEV) {
        close(p->fd);
        p->fd = -1;
    }
}

// the slot's effect stored on every device that can and started; 1 if one could
static int force_store(int slot, long long now) {
    const force* f = &forces[slot];
    const char* inv = getenv("MISTER_HYBRID_FORCE_INVERT");
    int sign = inv != NULL && atoi(inv) ? -1 : 1;
    int any = 0;
    int i;

    for (i = 0; i < MAX_PADS; i++) {
        pad* p = &pads[i];
        struct ff_effect effect;
        struct input_event ev;
        long long left;

        if (p->fd < 0 || !p->forces) {
            continue;
        }
        any = 1;
        if (f->kind == 0 || (f->kind == 2 && !p->sines)) {
            force_stop_on(p, slot);
            continue;
        }
        memset(&effect, 0, sizeof effect);
        effect.id = (short)p->force_id[slot];
        // 0x4000: across, the way a wheel turns; a positive level to the right
        effect.direction = 0x4000;
        left = f->end ? f->end - now : 2000;
        effect.replay.length = (unsigned short)(left < 1 ? 1 : left > 0x7fff ? 0x7fff : left);
        if (f->kind == 1) {
            effect.type = FF_CONSTANT;
            effect.u.constant.level = (short)(sign * f->level * 128);
        } else {
            effect.type = FF_PERIODIC;
            effect.u.periodic.waveform = FF_SINE;
            effect.u.periodic.period = (unsigned short)(f->period < 1 ? 1 : f->period);
            effect.u.periodic.magnitude = (short)(f->level * 128);
        }
        if (ioctl(p->fd, EVIOCSFF, &effect) < 0) {
            if (errno == ENODEV) {
                close(p->fd);
                p->fd = -1;
            }
            continue;
        }
        p->force_id[slot] = effect.id;
        memset(&ev, 0, sizeof ev);
        ev.type = EV_FF;
        ev.code = (unsigned short)effect.id;
        ev.value = 1;
        if (write(p->fd, &ev, sizeof ev) < 0) {
            continue;
        }
    }
    forces[slot].stored = now;
    return any;
}

int MH_ForceAvailable(void) {
    int i;

    refresh();
    for (i = 0; i < MAX_PADS; i++) {
        if (pads[i].fd >= 0 && pads[i].forces) {
            return 1;
        }
    }
    return 0;
}

static int force_set(int slot, int kind, int level, int period, int ms) {
    long long now = now_ms();
    force* f;

    if (slot < 0 || slot >= MH_FORCE_SLOTS) {
        return 0;
    }
    refresh();
    f = &forces[slot];
    level = level < -255 ? -255 : level > 255 ? 255 : level;
    if (kind == 2) {
        level = level < 0 ? -level : level;
    }
    if (level == 0) {
        kind = 0;
    }
    if (kind == f->kind && level == f->level && period == f->period && ms == 0 && f->end == 0 && kind != 0 &&
        now - f->stored < 1000) {
        return MH_ForceAvailable(); // the same, no end: kept going
    }
    f->kind = kind;
    f->level = level;
    f->period = period;
    f->end = ms > 0 ? now + ms : 0;
    return force_store(slot, now);
}

int MH_ForceConstant(int slot, int level, int ms) {
    return force_set(slot, 1, level, 0, ms);
}

int MH_ForceSine(int slot, int magnitude, int period_ms, int ms) {
    return force_set(slot, 2, magnitude, period_ms, ms);
}

void MH_ForceStop(int slot) {
    int i;

    for (i = 0; i < MH_FORCE_SLOTS; i++) {
        if (slot < 0 || i == slot) {
            force_set(i, 0, 0, 0, 0);
        }
    }
}

void MH_ForceKeep(void) {
    long long now = now_ms();
    int i;

    for (i = 0; i < MH_FORCE_SLOTS; i++) {
        if (forces[i].kind != 0 && forces[i].end == 0 && now - forces[i].stored >= 1000) {
            refresh();
            force_store(i, now);
        }
        if (forces[i].kind != 0 && forces[i].end != 0 && now >= forces[i].end) {
            forces[i].kind = 0; // (it ended by itself)
        }
    }
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

int mh_osd_hold(int* held, int pressed); // mister_hybrid.c

// How far a trigger is still let go, of 255
#define TRIGGER_REST 16

int MH_ReadStickButtons(int* left, int* right) {
    static int left_held, right_held;
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
    // Linux knows nothing of Main_MiSTer's OSD: nothing while it is open
    *left = mh_osd_hold(&left_held, *left);
    *right = mh_osd_hold(&right_held, *right);
    return found;
}

int MH_WheelConnected(void) {
    int i;

    refresh();
    for (i = 0; i < MAX_PADS; i++) {
        if (pads[i].fd >= 0 && pads[i].wheel) {
            return 1;
        }
    }
    return 0;
}

int MH_ReadTriggers(int* left, int* right) {
    static int left_held, right_held;
    const char* fixed = getenv("MISTER_HYBRID_TRIGGERS"); // "left,right": for tests without a controller
    int l = 0, r = 0, found = 0, wheel = 0;
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

        if (p->fd >= 0 && p->wheel) { // its pedals: from the core's sticks, below
            wheel = 1;
            continue;
        }
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
    if (wheel && MH_IsOpen()) {
        // a wheel's pedals as Main_MiSTer hands them to the core: the gas on the
        // left stick's up-down axis, the brake on the right one's, 0 to -127
        mh_input in;

        MH_ReadInput(&in);
        for (i = 0; i < 2; i++) {
            const int gas = in.analog_ly[i] < 0 ? -2 * in.analog_ly[i] : 0, brake = in.analog_ry[i] < 0 ? -2 * in.analog_ry[i] : 0;

            if (gas > r) {
                r = gas > 255 ? 255 : gas;
            }
            if (brake > l) {
                l = brake > 255 ? 255 : brake;
            }
        }
        found = 1;
    }
    // nothing while the OSD is open (Linux knows nothing of it), nor once it
    // has closed until the trigger is let go
    *left = mh_osd_hold(&left_held, l > TRIGGER_REST) ? l : 0;
    *right = mh_osd_hold(&right_held, r > TRIGGER_REST) ? r : 0;
    return found;
}
