// A message on the hybrid core's own picture (the purple screen), from
// outside the game: for the launch script, when the game has crashed or left
// with an error, and for testing. The core must be loaded; one of host
// version 13 or later shows it under its logo (MH_ShowMessage() of
// hps/mister_hybrid.h: 4 lines of 71 characters, wrapped at spaces and at
// "\n").
//
// usage: mh_message [--error] <text>   show it (and take the screen from a
//                                      game that left its frame there);
//                                      --error: the dots stand still
//        mh_message --clear            no message
//        mh_message --shown            exit code 0 while a message is shown
//                                      (the core's own as well), 1 else
//        mh_message --wait-reset       wait while the core is loaded: exit
//                                      code 44 when the player resets the
//                                      game (host version 18; the launch
//                                      script starts it again), 1 when
//                                      another core is loaded
// Exit code 2: no core, or one older than host version 13.
//
// build: arm-linux-gnueabihf-gcc -O2 -static -Ihybrid/hps -o mh_message
//        hybrid/tools/mh_message.c hybrid/hps/*.c -lpthread -lm

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mister_hybrid.h"

int main(int argc, char** argv) {
    int flags = 0;
    int i = 1;

    if (i < argc && strcmp(argv[i], "--error") == 0) {
        flags |= MH_MSG_ERROR;
        i++;
    }
    if (i != argc - 1) {
        fprintf(stderr, "usage: mh_message [--error] <text> | --clear | --shown | --wait-reset\n");
        return 2;
    }
    if (!MH_Open() || MH_HostVersion() < 13) {
        return 2;
    }
    if (strcmp(argv[i], "--clear") == 0) {
        MH_ClearMessage();
    } else if (strcmp(argv[i], "--wait-reset") == 0) {
        // the library ends this with exit code 44 at a reset (MH_EXIT_RESET)
        while (MH_CheckAlive()) {
            usleep(100000);
        }
        return 1;
    } else if (strcmp(argv[i], "--shown") == 0) {
        // the core reads the message in its next vblank
        return MH_MessageShown() ? 0 : 1;
    } else {
        // "\n" in an argument from a script: a new line
        static char text[4096];
        const char* s = argv[i];
        size_t n = 0;
        while (*s != 0 && n < sizeof(text) - 1) {
            if (s[0] == '\\' && s[1] == 'n') {
                text[n++] = '\n';
                s += 2;
            } else {
                text[n++] = *s++;
            }
        }
        text[n] = 0;
        MH_ShowMessage(text, flags);
    }
    return 0;
}
