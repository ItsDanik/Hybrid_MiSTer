// The game's logo at its start (host version 17, docs/game_logo.md) against
// tools/fakecore.c: tools/logo_test.sh runs it. A game "Doom" (id doom, its
// file DOOM.WAD in the current folder); the logo is logos/doom.mhl next to
// this program.
//   logo_test game <load ms>  picked, loaded for that long, then its palette,
//                             mode and 60 frames; each step with its field
//   logo_test exec            picked, then started again in its own place
//                             (MH_Close(), execv(), as CrispyDoom's Heretic
//                             is), which loads for a second and shows frames
//   logo_test missing         its menu entry (MISTER_HYBRID_SET=Logo_doom)
//                             without DOOM.WAD: the logo and the message
//   logo_test page            a program with no files to check and its own
//                             page on the overlay (MH_Overlay(), the MIDI
//                             Player): its logo (logos/doom.mhl), the line
//                             "Loading Doom...", then 60 frames of 640x480
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mister_hybrid.h"

static const char* const doom_files[] = { "DOOM.WAD", NULL };
static const mh_game games[] = {
    { "doom", "Doom", doom_files, NULL, "Doom is not there: copy DOOM.WAD to %s/." },
};

static const char* const no_files[] = { NULL };
static const mh_game page_game[] = {
    { "doom", "Doom", no_files, NULL, NULL },
};

static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

// load, then the game's first frames (640x480 on the overlay)
static void run(int load_ms, int page) {
    static uint8_t frame[640 * 480];
    uint32_t pal[256];
    uint32_t field;
    int i;

    sleep_ms(load_ms);
    printf("logo_test: field %u: loaded\n", MH_FieldCounter());
    MH_SetFormat(MH_FORMAT_INDEX8);
    MH_SetMode(page ? MH_MODE_640x480 : MH_MODE_320x200);
    for (i = 0; i < 256; i++) {
        pal[i] = (uint32_t)i * 0x010101;
    }
    MH_SetPalette(pal);
    printf("logo_test: field %u: palette set\n", MH_FieldCounter());
    field = MH_FieldCounter();
    for (i = 0; i < 60 && MH_IsOpen(); i++) {
        memset(frame, i + 1, sizeof(frame));
        MH_Present(frame, page ? 640 : 320);
        if (i == 0) {
            printf("logo_test: field %u: first frame presented\n", MH_FieldCounter());
        }
        MH_WaitFrame(field);
        field = MH_FieldCounter();
    }
    printf("logo_test: field %u: done\n", MH_FieldCounter());
    fflush(stdout);
}

int main(int argc, char** argv) {
    int listed, pick;

    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: logo_test game <load ms> | exec | missing | page\n");
        return 2;
    }
    if (!MH_Open()) {
        fprintf(stderr, "logo_test: no core\n");
        return 1;
    }
    if (strcmp(argv[1], "child") == 0) {
        printf("logo_test: field %u: started again\n", MH_FieldCounter());
        run(1000, 0);
        MH_Close();
        return 0;
    }
    if (strcmp(argv[1], "page") == 0) {
        MH_Overlay(1);
        pick = MH_PickGame(page_game, 1, NULL, "No game", &listed);
        printf("logo_test: field %u: picked %d\n", MH_FieldCounter(), pick);
        run(500, 1);
        MH_Close();
        return 0;
    }
    pick = MH_PickGame(games, 1, NULL, "No game: copy DOOM.WAD to %s/.", &listed);
    printf("logo_test: field %u: picked %d\n", MH_FieldCounter(), pick);
    if (pick < 0) {
        MH_Close();
        return 0;
    }
    if (strcmp(argv[1], "exec") == 0) {
        sleep_ms(300);
        MH_Close();
        execl("/proc/self/exe", argv[0], "child", (char*)NULL);
        perror("execl");
        return 1;
    }
    run(argc > 2 ? atoi(argv[2]) : 500, 0);
    MH_Close();
    return 0;
}
