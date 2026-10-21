// A test of the core's MIDI synthesizer, for the PC (with fakecore) and the
// MiSTer: plays a MIDI file through the library (MH_Midi*()) as a game would,
// its events sent from the audio loop at their frames, with silence in the
// audio ring. The SoundFont is the OSD's choice (MH_SoundFontDir()).
//
//   PC:      gcc -O2 -o fakecore tools/fakecore.c
//            gcc -O2 -Ihps -o midiplay tools/midiplay.c tools/midisynth/smf.c hps/mister_hybrid.c
//                hps/mister_joymap.c hps/mister_ui.c hps/mister_triggers.c hps/mister_games.c
//                hps/mister_midi.c -lpthread -lm
//            ./fakecore /tmp/shm script audio.raw &
//            MISTER_HYBRID_SHM=/tmp/shm MISTER_HYBRID_SOUNDFONTS=<folder> ./midiplay song.mid
//   MiSTer:  build it with the toolchain, load any hybrid core of host
//            version 16 or later (15 with an MT32-pi) with its game kept from starting
//            (touch /tmp/<name>_nolaunch) and run it.
// usage: midiplay <song.mid> [volume] [seconds]
//   volume: MH_MidiVolume(), 32 as it comes; seconds: stop after that many
// Every 5 seconds it prints where it is. MIDIPLAY_MODE=<mode>[,565] shows
// that video mode (MH_MODE_*, RGB565 with ",565") with a moving picture, a
// frame every field, for testing the synthesizer's memory reads against the
// video's; without it a black 320x200 frame, once.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mister_hybrid.h"
#include "midisynth/smf.h"

#define BLOCK 512

static int rgb;

// a moving picture: diagonal bands and a bar, drawn once twice as tall as
// the screen and shown from a row further down each time (the copy is all
// the work per frame, as a game's)
static void picture(int n) {
    static uint8_t pixels[640 * 960 * 2];
    static int drawn;
    int w = MH_Width(), h = MH_Height(), bpp = rgb ? 2 : 1, x, y;

    if (!drawn) {
        for (y = 0; y < 2 * h; y++) {
            for (x = 0; x < w; x++) {
                int v = ((x + y) >> 3) & 63, bar = y % h < 8;
                if (rgb) {
                    ((uint16_t*)pixels)[y * w + x] = bar ? 0xffff : (uint16_t)(v << 5 | (x * 31 / w) << 11 | (y % h * 31 / h));
                } else {
                    pixels[y * w + x] = bar ? 255 : (uint8_t)(v * 4);
                }
            }
        }
        drawn = 1;
    }
    MH_Present(pixels + (size_t)(n % h) * w * bpp, w * bpp);
}

int main(int argc, char** argv) {
    static uint32_t silence[BLOCK];
    char err[200];
    smf_song song;
    uint32_t frame, end, told = 0;
    int e = 0, volume = argc > 2 ? atoi(argv[2]) : 32, i, n = 0;
    uint32_t field = 0;
    double seconds = argc > 3 ? atof(argv[3]) : 0;
    const char* mode = getenv("MIDIPLAY_MODE");

    if (argc < 2) {
        fprintf(stderr, "usage: midiplay <song.mid> [volume] [seconds]\n");
        return 1;
    }
    if (!smf_load(argv[1], &song, err, sizeof(err))) {
        fprintf(stderr, "midiplay: %s %s\n", argv[1], err);
        return 1;
    }
    if (!MH_Open()) {
        fprintf(stderr, "midiplay: no hybrid core\n");
        return 1;
    }
    // a game is one that has shown a frame: a black one, or the picture
    if (mode) {
        static uint32_t palette[256];
        for (i = 0; i < 256; i++) {
            palette[i] = (uint32_t)i << 16 | (uint32_t)(255 - i) << 8 | (uint32_t)(i * 3 & 255);
        }
        rgb = strstr(mode, ",565") != NULL;
        MH_SetFormat(rgb ? MH_FORMAT_RGB565 : MH_FORMAT_INDEX8);
        MH_SetPalette(palette);
        if (!MH_SetMode(atoi(mode))) {
            fprintf(stderr, "midiplay: video mode %d not available\n", atoi(mode));
        }
        picture(0);
    } else {
        static uint8_t black[320 * 200];
        static uint32_t palette[256];
        MH_SetFormat(MH_FORMAT_INDEX8);
        MH_SetPalette(palette);
        MH_Present(black, 320);
    }
    MH_AudioEnable(1);
    if (!MH_MidiAvailable()) {
        fprintf(stderr, "midiplay: this core plays no MIDI (host version 15: an MT32-pi, 16: the synthesizer too)\n");
        MH_Close();
        return 1;
    }
    MH_MidiEnable(1);
    MH_MidiVolume(volume);
    // the SoundFont loads meanwhile: the song starts when it is there
    for (i = 0; !MH_MidiReady() && MH_MidiWanted() && MH_CheckAlive(); i += BLOCK) {
        MH_AudioWrite(silence, BLOCK, 2048);
    }
    if (!MH_MidiWanted()) {
        fprintf(stderr, "midiplay: no MIDI: the OSD says FM chip, or no SoundFont and no MT32-pi\n");
        MH_AudioEnable(0);
        MH_Close();
        return 1;
    }
    printf("midiplay: ready in %.2f s, playing %s (%.1f s)\n", i / 44100.0, argv[1], song.length);
    fflush(stdout);

    end = (uint32_t)((seconds > 0 && seconds < song.length ? seconds : song.length) * MH_AUDIO_RATE);
    for (frame = 0; frame < end && MH_CheckAlive(); frame += BLOCK) {
        while (e < song.count && (uint32_t)(song.events[e].seconds * MH_AUDIO_RATE + 0.5) < frame + BLOCK) {
            const smf_event* ev = &song.events[e++];
            int at = (int)((uint32_t)(ev->seconds * MH_AUDIO_RATE + 0.5) - frame);

            if (at < 0) {
                at = 0;
            }
            if (ev->status == 0xf0) {
                MH_MidiSysEx(at, ev->data, ev->length);
            } else if (ev->status != 0xff) {
                MH_MidiEvent(at, ev->status, ev->data1, ev->data2);
            }
        }
        MH_AudioWrite(silence, BLOCK, 2048);
        // a frame a field, as a game
        if (mode && MH_FieldCounter() != field) {
            field = MH_FieldCounter();
            picture(n += 2);
        }
        if (frame - told >= 5 * MH_AUDIO_RATE) {
            told = frame;
            printf("midiplay: %.0f s\n", frame / 44100.0);
            fflush(stdout);
        }
    }
    // the releases die away
    MH_MidiReset(0);
    for (frame = 0; frame < 2 * MH_AUDIO_RATE && MH_CheckAlive(); frame += BLOCK) {
        MH_AudioWrite(silence, BLOCK, 2048);
    }
    MH_MidiEnable(0);
    MH_AudioEnable(0);
    MH_Close();
    smf_free(&song);
    printf("midiplay: done\n");
    return 0;
}
