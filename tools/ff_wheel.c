// A racing wheel with force feedback that is not there: a uinput device with
// a wheel axis, two pedals, buttons and FF_CONSTANT, FF_PERIODIC (FF_SINE) and
// FF_GAIN, that prints every effect a game stores, starts and stops. For
// trying MH_Force* (hps/mister_triggers.c) on a PC, or on the MiSTer, without
// a wheel. Needs write access to /dev/uinput.
//
// usage: ff_wheel [seconds]        (default: until killed)
// Each line: milliseconds since the start, then what happened:
//   upload <id> constant level <l> dir <d> length <ms>
//   upload <id> sine magnitude <m> period <ms> dir <d> length <ms>
//   play <id> / stop <id> / erase <id> / gain <g>

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static long long now_ms(void) {
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int main(int argc, char** argv) {
    struct uinput_setup setup;
    struct uinput_abs_setup abs;
    int seconds = argc > 1 ? atoi(argv[1]) : 0;
    long long start = now_ms();
    int fd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
    static const int axes[3] = { ABS_X, ABS_Y, ABS_Z }; // the wheel, the gas, the brake
    int i;

    if (fd < 0) {
        perror("/dev/uinput");
        return 1;
    }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_FF);
    for (i = BTN_TRIGGER; i <= BTN_BASE6; i++) {
        ioctl(fd, UI_SET_KEYBIT, i);
    }
    for (i = 0; i < 3; i++) {
        ioctl(fd, UI_SET_ABSBIT, axes[i]);
        memset(&abs, 0, sizeof abs);
        abs.code = (unsigned short)axes[i];
        abs.absinfo.minimum = 0;
        abs.absinfo.maximum = i == 0 ? 16383 : 255;
        abs.absinfo.value = i == 0 ? 8192 : 0;
        ioctl(fd, UI_ABS_SETUP, &abs);
    }
    ioctl(fd, UI_SET_FFBIT, FF_CONSTANT);
    ioctl(fd, UI_SET_FFBIT, FF_PERIODIC);
    ioctl(fd, UI_SET_FFBIT, FF_SINE);
    ioctl(fd, UI_SET_FFBIT, FF_GAIN);
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1d6b;
    setup.id.product = 0x0106;
    snprintf(setup.name, sizeof setup.name, "hybrid test wheel");
    setup.ff_effects_max = 16;
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("uinput");
        return 1;
    }
    printf("0 the wheel is there\n");
    fflush(stdout);
    while (!seconds || now_ms() - start < seconds * 1000LL) {
        struct pollfd p = { fd, POLLIN, 0 };
        struct input_event ev;

        if (poll(&p, 1, 100) <= 0) {
            continue;
        }
        while (read(fd, &ev, sizeof ev) == sizeof ev) {
            long long t = now_ms() - start;

            if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
                struct uinput_ff_upload up;
                const struct ff_effect* e = &up.effect;

                memset(&up, 0, sizeof up);
                up.request_id = (unsigned)ev.value;
                ioctl(fd, UI_BEGIN_FF_UPLOAD, &up);
                if (e->type == FF_CONSTANT) {
                    printf("%lld upload %d constant level %d dir 0x%04x length %d\n", t, e->id, e->u.constant.level,
                           e->direction, e->replay.length);
                } else if (e->type == FF_PERIODIC) {
                    printf("%lld upload %d %s magnitude %d period %d dir 0x%04x length %d\n", t, e->id,
                           e->u.periodic.waveform == FF_SINE ? "sine" : "periodic", e->u.periodic.magnitude,
                           e->u.periodic.period, e->direction, e->replay.length);
                } else {
                    printf("%lld upload %d type 0x%x\n", t, e->id, e->type);
                }
                up.retval = 0;
                ioctl(fd, UI_END_FF_UPLOAD, &up);
            } else if (ev.type == EV_UINPUT && ev.code == UI_FF_ERASE) {
                struct uinput_ff_erase er;

                memset(&er, 0, sizeof er);
                er.request_id = (unsigned)ev.value;
                ioctl(fd, UI_BEGIN_FF_ERASE, &er);
                printf("%lld erase %u\n", t, er.effect_id);
                er.retval = 0;
                ioctl(fd, UI_END_FF_ERASE, &er);
            } else if (ev.type == EV_FF && ev.code == FF_GAIN) {
                printf("%lld gain %d\n", t, ev.value);
            } else if (ev.type == EV_FF) {
                printf("%lld %s %d\n", t, ev.value ? "play" : "stop", ev.code);
            }
            fflush(stdout);
        }
    }
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
