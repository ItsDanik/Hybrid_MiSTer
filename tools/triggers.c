// Prints what MH_ReadTriggers() reads, ten times a second: to see on a MiSTer
// whether a controller's triggers are found and how far they go.
// build:  arm-linux-gnueabihf-gcc -O2 -static -Ihps -o triggers tools/triggers.c hps/*.c -lpthread -lm
// usage:  triggers [seconds]
// With MISTER_HYBRID_CORE=<the core's name> it attaches to the running core
// first, so that a racing wheel's pedals (the core's sticks) are read as well,
// and prints the sticks as the core has them (player 1's and 2's, x / y).
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mister_hybrid.h"

int main(int argc, char** argv) {
    int n = (argc > 1 ? atoi(argv[1]) : 10) * 10;
    int i;

    if (getenv("MISTER_HYBRID_CORE") != NULL) {
        MH_Open();
    }
    for (i = 0; i < n; i++) {
        int l = 0, r = 0;
        int found = MH_ReadTriggers(&l, &r);

        printf("%s  left %3d  right %3d", found ? "triggers" : "none    ", l, r);
        if (MH_IsOpen()) {
            mh_input in;
            int p;

            MH_ReadInput(&in);
            for (p = 0; p < 2; p++) {
                printf("   p%d left %4d %4d right %4d %4d", p + 1, in.analog_lx[p], in.analog_ly[p], in.analog_rx[p],
                    in.analog_ry[p]);
            }
        }
        printf("\n");
        fflush(stdout);
        usleep(100000);
    }
    return 0;
}
