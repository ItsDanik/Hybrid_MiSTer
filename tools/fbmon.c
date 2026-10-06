// Watch which frame a hybrid core shows in every field (run on the MiSTer with
// the core loaded and a game running). A game that presents one frame per
// field shows a new framebuffer in every field; a frame that came too late
// for its vblank shows as a field that repeats the framebuffer before it,
// which the game itself cannot see.
//
// usage: fbmon [seconds]   (default 10; one line per second and a total)
// build: arm-linux-gnueabihf-gcc -O2 -o fbmon fbmon.c

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char** argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 10;
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    volatile uint32_t* status;
    uint32_t field, fb, last_fb = 0xff;
    unsigned fields = 0, repeats = 0, missed = 0, total_fields = 0, total_repeats = 0, total_missed = 0;
    struct timespec ts = { 0, 300000 };
    int second = 0;
    void* m;

    if (fd < 0 || (m = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, 0x30000000)) == MAP_FAILED) {
        perror("/dev/mem");
        return 1;
    }
    status = (volatile uint32_t*)((uint8_t*)m + 0x40);
    field = status[1];
    while (second < seconds) {
        uint32_t f = status[1];

        if (f == field) {
            nanosleep(&ts, NULL);
            continue;
        }
        // fields this program slept through
        missed += f - field - 1;
        field = f;
        fb = status[2] & 0xff;
        if (fb == last_fb) {
            repeats++;
        }
        last_fb = fb;
        if (++fields >= 60) {
            printf("%2d: %u fields, %u showed the frame of the field before, %u not seen\n", second, fields, repeats, missed);
            fflush(stdout);
            total_fields += fields;
            total_repeats += repeats;
            total_missed += missed;
            fields = repeats = missed = 0;
            second++;
        }
    }
    printf("total: %u fields, %u repeats, %u not seen\n", total_fields, total_repeats, total_missed);
    return 0;
}
