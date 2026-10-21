// The commands a song makes, for sim/midi_engine.cpp: the library
// (hps/mister_midi.c) plays it with a SoundFont as tools/midisynth/render.c
// does, with the C model reporting its voices at every field, and every
// command it sends is kept with the frame it was sent at.
#include "../tools/midisynth/midisynth.h"
#include "../tools/midisynth/smf.h"
#include "../hps/mister_midi.h"
#include "midi_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int16_t samples[MH_MIDI_MAX_SAMPLES];
static gen_cmd_t* cmds;
static long ncmds, cap;
static uint32_t now;
static midisynth* model;

static void emit(void* ctx, const uint64_t* cmd) {
    (void)ctx;
    if (ncmds == cap) {
        cap = cap ? cap * 2 : 4096;
        cmds = realloc(cmds, (size_t)cap * sizeof(*cmds));
    }
    cmds[ncmds].sent = now;
    memcpy(cmds[ncmds].w, cmd, sizeof(cmds[ncmds].w));
    ncmds++;
    ms_push(model, cmd);
}

static void send(mh_midi* m, uint32_t frame, const smf_event* ev) {
    if (ev->status == 0xf0) {
        mh_midi_sysex(m, frame, ev->data, ev->length);
    } else if (ev->status != 0xff) {
        mh_midi_event(m, frame, ev->status, ev->data1, ev->data2);
    }
}

long gen_song(const char* font_path, const char* song_path, double seconds, uint32_t* frames) {
    char err[200];
    mh_sfont* font;
    mh_midi* m;
    smf_song song;
    uint32_t end, bits[4];
    int e = 0;

    ncmds = 0;
    font = mh_sfont_load(font_path, samples, MH_MIDI_MAX_SAMPLES, err, sizeof(err));
    if (font == NULL) {
        fprintf(stderr, "%s %s\n", font_path, err);
        return -1;
    }
    if (!smf_load(song_path, &song, err, sizeof(err))) {
        fprintf(stderr, "%s %s\n", song_path, err);
        return -1;
    }
    model = ms_new(samples, MH_MIDI_MAX_SAMPLES);
    m = mh_midi_new(emit, NULL);
    mh_midi_set_font(m, font);
    end = (uint32_t)(song.length * MH_MIDI_RATE) + 3 * MH_MIDI_RATE;
    if (seconds > 0 && seconds * MH_MIDI_RATE < end) {
        end = (uint32_t)(seconds * MH_MIDI_RATE);
    }
    for (now = 0; now < end; now++) {
        int64_t sums[4];

        while (e < song.count && (uint32_t)(song.events[e].seconds * MH_MIDI_RATE + 0.5) <= now) {
            send(m, now, &song.events[e++]);
        }
        mh_midi_flush(m);
        if (now % 735 == 0) {
            ms_playing(model, bits);
            mh_midi_voices(m, now, bits);
        }
        ms_frame(model, now, sums);
    }
    mh_midi_free(m);
    mh_sfont_free(font);
    ms_free(model);
    *frames = end;
    return ncmds;
}

const gen_cmd_t* gen_cmds(void) {
    return cmds;
}

const int16_t* gen_samples(void) {
    return samples;
}
