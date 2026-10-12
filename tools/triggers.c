// Prints what MH_ReadTriggers() reads, ten times a second: to see on a MiSTer
// whether a controller's triggers are found and how far they go.
// build:  arm-linux-gnueabihf-gcc -O2 -static -Ihps -o triggers tools/triggers.c hps/mister_triggers.c
// usage:  triggers [seconds]
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mister_hybrid.h"

int main(int argc, char** argv) {
    int n = (argc > 1 ? atoi(argv[1]) : 10) * 10;
    int i;

    for (i = 0; i < n; i++) {
        int l = 0, r = 0;
        int found = MH_ReadTriggers(&l, &r);

        printf("%s  left %3d  right %3d\n", found ? "triggers" : "none    ", l, r);
        fflush(stdout);
        usleep(100000);
    }
    return 0;
}
