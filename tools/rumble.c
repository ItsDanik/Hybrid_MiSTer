// Shakes every gamepad that can rumble for half a second, straight through
// Linux (evdev force feedback), and says what happened at each step: the test
// of whether a game on a MiSTer can reach a controller's motors while
// Main_MiSTer holds the controller.
// build:  arm-linux-gnueabihf-gcc -O2 -static -o rumble tools/rumble.c
// usage:  rumble [milliseconds]
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char** argv) {
    int ms = argc > 1 ? atoi(argv[1]) : 500;
    DIR* dir = opendir("/dev/input");
    struct dirent* e;
    int any = 0;

    while (dir != NULL && (e = readdir(dir)) != NULL) {
        unsigned long ff[FF_MAX / (8 * sizeof(long)) + 1];
        char path[300], name[128] = "?";
        struct ff_effect effect;
        struct input_event play;
        int fd;

        if (strncmp(e->d_name, "event", 5) != 0) {
            continue;
        }
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        fd = open(path, O_RDWR);
        if (fd < 0) {
            continue;
        }
        memset(ff, 0, sizeof ff);
        if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff), ff) < 0 ||
            !((ff[FF_RUMBLE / (8 * sizeof(long))] >> (FF_RUMBLE % (8 * sizeof(long)))) & 1)) {
            close(fd);
            continue;
        }
        any = 1;
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        printf("%s (%s): can rumble\n", path, name);

        memset(&effect, 0, sizeof effect);
        effect.type = FF_RUMBLE;
        effect.id = -1;
        effect.u.rumble.strong_magnitude = 0xc000;
        effect.u.rumble.weak_magnitude = 0x8000;
        effect.replay.length = (unsigned short)ms;
        if (ioctl(fd, EVIOCSFF, &effect) < 0) {
            printf("  the effect was refused: %s\n", strerror(errno));
            close(fd);
            continue;
        }
        printf("  effect %d stored\n", effect.id);
        memset(&play, 0, sizeof play);
        play.type = EV_FF;
        play.code = (unsigned short)effect.id;
        play.value = 1;
        if (write(fd, &play, sizeof play) != (ssize_t)sizeof play) {
            printf("  starting it failed: %s\n", strerror(errno));
        } else {
            printf("  started, for %d ms\n", ms);
        }
        usleep((ms + 100) * 1000);
        if (ioctl(fd, EVIOCRMFF, effect.id) < 0) {
            printf("  removing it failed: %s\n", strerror(errno));
        }
        close(fd);
    }
    if (dir != NULL) {
        closedir(dir);
    }
    if (!any) {
        printf("no connected controller can rumble\n");
    }
    return 0;
}
