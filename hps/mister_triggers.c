// Analog triggers (L2 / R2), for MH_ReadTriggers() (mister_hybrid.h).
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

#include "mister_hybrid.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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
} pad;

static pad pads[MAX_PADS];
static int pads_ready;
static time_t scanned;

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
    while (dir != NULL && (e = readdir(dir)) != NULL) {
        char path[300];
        int fd, free_slot = -1;

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
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        if (!is_gamepad(fd)) {
            close(fd);
            continue;
        }
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

int MH_ReadTriggers(int* left, int* right) {
    const char* fixed = getenv("MISTER_HYBRID_TRIGGERS"); // "left,right": for tests without a controller
    time_t now = time(NULL);
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
