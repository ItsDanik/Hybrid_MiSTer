// A test of host version 17's MIDI calls for a player (docs/midiplayer.md):
// the SoundFont slots, a font loaded by name, where the music goes. It checks
// itself and ends with PASS or FAIL. tools/midiapi_test.sh runs all three.
//
//   midiapi_test slots <empty folder>
//       MH_SoundFontSlots() / MH_SetSoundFontSlot() on empty .sf2 files it
//       makes there: each rule of slots.txt. No core needed.
//   midiapi_test load
//       MH_MidiLoadSoundFont() / MH_MidiSoundFont() against fakecore, the
//       folder (MISTER_HYBRID_SOUNDFONTS) holding AWE64.sf2,
//       GeneralUser-GS.sf2 and Roland SC-55.sf2, no slots.txt; fakecore's
//       script sets the OSD's slot to 2 after 10 s ("wait 600", "soundfont 2").
//   midiapi_test dest <fakecore's MIDI log>
//       MH_MidiDest() against fakecore with FAKECORE_MT32=1 and
//       FAKECORE_MIDILOG=<log>; its script turns the OSD's MT32-pi line off
//       after 10 s ("wait 600", "osd2 1").
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "mister_hybrid.h"

#define BLOCK 512

static int failures;
static volatile int quit, notes;

static void check(int ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

static double now(void) {
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static const char* base(const char* path) {
    const char* slash = strrchr(path, '/');

    return slash != NULL ? slash + 1 : path;
}

// --- slots ---------------------------------------------------------------

static void touch(const char* dir, const char* name) {
    char path[512];
    FILE* f;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "w");
    if (f != NULL) {
        fclose(f);
    }
}

static void remove_file(const char* dir, const char* name) {
    char path[512];

    snprintf(path, sizeof path, "%s/%s", dir, name);
    unlink(path);
}

static void write_slots(const char* dir, const char* text) {
    char path[512];
    FILE* f;

    snprintf(path, sizeof path, "%s/slots.txt", dir);
    f = fopen(path, "w");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static int slots_are(const char* expect) {
    // expect: "file own|file own|..." for the 8 slots
    static char files[MH_SF_SLOTS][MH_SF_NAME];
    char got[2048] = "";
    int own[MH_SF_SLOTS], k, n = 0;

    MH_SoundFontSlots(files, own);
    for (k = 0; k < MH_SF_SLOTS; k++) {
        n += snprintf(got + n, sizeof got - (size_t)n, "%s%s %d", k ? "|" : "", files[k], own[k]);
    }
    if (strcmp(got, expect) != 0) {
        printf("      got    %s\n      wanted %s\n", got, expect);
    }
    return strcmp(got, expect) == 0;
}

static int test_slots(const char* dir) {
    char files[MH_SF_SLOTS][MH_SF_NAME];
    int own[MH_SF_SLOTS];

    setenv("MISTER_HYBRID_SOUNDFONTS", dir, 1);
    remove_file(dir, "slots.txt");
    check(MH_SoundFontSlots(files, own) == 0, "an empty folder has no font");

    touch(dir, "b.sf2");
    touch(dir, "A.sf2");
    touch(dir, "GeneralUser-GS.sf2");
    touch(dir, "notes.txt");
    check(slots_are("GeneralUser-GS.sf2 1|A.sf2 1|b.sf2 1|GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0|"
                    "GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0"),
          "no slots.txt: 0 GeneralUser-GS, then the others by name, case ignored");

    check(MH_SetSoundFontSlot(4, "b.sf2"), "slot 4 set");
    check(slots_are("GeneralUser-GS.sf2 1|A.sf2 1|b.sf2 1|GeneralUser-GS.sf2 0|b.sf2 1|"
                    "GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0"),
          "the first write keeps every other slot where it was");

    touch(dir, "0first.sf2");
    check(slots_are("GeneralUser-GS.sf2 1|A.sf2 1|b.sf2 1|GeneralUser-GS.sf2 0|b.sf2 1|"
                    "GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0"),
          "a new font moves no slot once slots.txt is there");

    check(MH_SetSoundFontSlot(1, NULL), "slot 1 cleared");
    check(slots_are("GeneralUser-GS.sf2 1|GeneralUser-GS.sf2 0|b.sf2 1|GeneralUser-GS.sf2 0|b.sf2 1|"
                    "GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0"),
          "an empty slot plays slot 0's");

    check(MH_SetSoundFontSlot(0, "A.sf2"), "slot 0 set");
    check(slots_are("A.sf2 1|A.sf2 0|b.sf2 1|A.sf2 0|b.sf2 1|A.sf2 0|A.sf2 0|A.sf2 0"),
          "the empty slots follow slot 0");

    remove_file(dir, "b.sf2");
    check(slots_are("A.sf2 1|A.sf2 0|A.sf2 0|A.sf2 0|A.sf2 0|A.sf2 0|A.sf2 0|A.sf2 0"),
          "a slot whose file is gone is empty");

    write_slots(dir, "# by hand\n\n3 A.sf2\r\n9 A.sf2\nx A.sf2\n5  A.sf2\n");
    check(slots_are("GeneralUser-GS.sf2 1|GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0|A.sf2 1|GeneralUser-GS.sf2 0|"
                    "GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0|GeneralUser-GS.sf2 0"),
          "slots.txt without slot 0: the old rule for it (GeneralUser-GS); bad lines skipped");
    remove_file(dir, "GeneralUser-GS.sf2");
    check(slots_are("0first.sf2 1|0first.sf2 0|0first.sf2 0|A.sf2 1|0first.sf2 0|0first.sf2 0|0first.sf2 0|0first.sf2 0"),
          "and without GeneralUser-GS: the first by name");

    check(!MH_SetSoundFontSlot(8, "A.sf2"), "slot 8 refused");
    check(!MH_SetSoundFontSlot(2, "x/A.sf2"), "a path refused");
    return failures == 0;
}

// --- with fakecore -------------------------------------------------------

// the audio loop: silence, and in dest a note every quarter of a second
static void* audio(void* arg) {
    static uint32_t silence[BLOCK];
    int n = 0;

    (void)arg;
    while (!quit) {
        if (notes && n % 21 == 0) {
            MH_MidiEvent(0, 0x90, 60, 100);
        } else if (notes && n % 21 == 10) {
            MH_MidiEvent(0, 0x80, 60, 0);
        }
        n++;
        MH_AudioWrite(silence, BLOCK, 2048);
    }
    return NULL;
}

static int wait_state(int want, double seconds, char* file, int size) {
    double until = now() + seconds;
    int state;

    while ((state = MH_MidiSoundFont(file, size)) != want && now() < until) {
        usleep(10000);
    }
    return state;
}

static void wait_until(double t) {
    while (now() < t) {
        usleep(10000);
    }
}

static long file_size(const char* path) {
    struct stat st;

    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static int test_load(void) {
    char file[512];
    double start = now();
    int state;

    state = wait_state(MH_SF_LOADED, 10, file, sizeof file);
    check(state == MH_SF_LOADED && strcmp(base(file), "GeneralUser-GS.sf2") == 0, "the OSD's slot 0 at the start: GeneralUser-GS");

    check(MH_MidiLoadSoundFont("AWE64.sf2"), "AWE64 asked for");
    state = MH_MidiSoundFont(file, sizeof file);
    check(state == MH_SF_LOADING && strcmp(base(file), "AWE64.sf2") == 0, "at once: loading, AWE64 coming");
    check(!MH_MidiReady() && MH_MidiWanted(), "not ready while it loads, still wanted");
    state = wait_state(MH_SF_LOADED, 10, file, sizeof file);
    check(state == MH_SF_LOADED && strcmp(base(file), "AWE64.sf2") == 0, "AWE64 loaded");
    check(MH_MidiReady(), "ready");

    MH_MidiLoadSoundFont("missing.sf2");
    state = wait_state(MH_SF_FAILED, 10, file, sizeof file);
    check(state == MH_SF_FAILED && file[0] == 0, "a missing file: failed");
    check(!MH_MidiWanted() && !MH_MidiReady(), "then the music is not wanted");

    MH_MidiLoadSoundFont("AWE64.sf2");
    state = wait_state(MH_SF_LOADED, 10, file, sizeof file);
    check(state == MH_SF_LOADED && strcmp(base(file), "AWE64.sf2") == 0, "AWE64 again");

    // fakecore sets the OSD's slot to 2 at 10 s
    wait_until(start + 12);
    state = MH_MidiSoundFont(file, sizeof file);
    check(state == MH_SF_LOADED && strcmp(base(file), "AWE64.sf2") == 0, "the OSD's slot changed: the program's font stays");

    MH_MidiLoadSoundFont(NULL);
    state = wait_state(MH_SF_LOADED, 10, file, sizeof file);
    usleep(200000);
    state = wait_state(MH_SF_LOADED, 10, file, sizeof file);
    check(state == MH_SF_LOADED && strcmp(base(file), "Roland SC-55.sf2") == 0, "back to the OSD: its slot 2, Roland SC-55");
    return failures == 0;
}

static int test_dest(const char* log) {
    double start = now();
    long a, b;

    check(MH_MidiPiConnected(), "the MT32-pi is connected");
    wait_state(MH_SF_LOADED, 10, NULL, 0);
    notes = 1;
    MH_MidiSynthChanged();

    a = file_size(log);
    usleep(1000000);
    b = file_size(log);
    check(b > a, "the OSD's MT32-pi line on: notes go to the Pi");

    MH_MidiDest(MH_MIDI_SYNTH);
    usleep(300000);
    check(MH_MidiSynthChanged(), "switched to the synthesizer: MH_MidiSynthChanged() says so");
    a = file_size(log);
    usleep(1000000);
    b = file_size(log);
    check(b == a && MH_MidiReady(), "the program's choice, the synthesizer: nothing goes to the Pi");

    MH_MidiDest(MH_MIDI_PI);
    usleep(300000);
    check(MH_MidiSynthChanged(), "switched to the Pi");
    // fakecore turns the OSD's MT32-pi line off at 10 s
    wait_until(start + 11);
    a = file_size(log);
    usleep(1000000);
    b = file_size(log);
    check(b > a, "the OSD's line off: the program's choice, the Pi, still plays");

    MH_MidiDest(MH_MIDI_OSD);
    usleep(300000);
    a = file_size(log);
    usleep(1000000);
    b = file_size(log);
    check(b == a && MH_MidiWanted(), "back to the OSD: its line is off, the synthesizer plays");
    notes = 0;
    return failures == 0;
}

int main(int argc, char** argv) {
    pthread_t thread;
    int ok;

    if (argc >= 3 && strcmp(argv[1], "slots") == 0) {
        ok = test_slots(argv[2]);
    } else if (argc >= 2 && (strcmp(argv[1], "load") == 0 || (strcmp(argv[1], "dest") == 0 && argc >= 3))) {
        if (!MH_Open()) {
            fprintf(stderr, "no core (fakecore)\n");
            return 2;
        }
        // the core plays a program's sound once it has shown a frame
        static uint8_t black[320 * 200];
        static uint32_t palette[256];
        MH_SetFormat(MH_FORMAT_INDEX8);
        MH_SetPalette(palette);
        MH_Present(black, 320);
        MH_AudioEnable(1);
        MH_MidiEnable(1);
        pthread_create(&thread, NULL, audio, NULL);
        ok = strcmp(argv[1], "load") == 0 ? test_load() : test_dest(argv[2]);
        quit = 1;
        pthread_join(thread, NULL);
        MH_AudioEnable(0);
        MH_Close();
    } else {
        fprintf(stderr, "usage: midiapi_test slots <folder> | load | dest <midi log>\n");
        return 2;
    }
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
