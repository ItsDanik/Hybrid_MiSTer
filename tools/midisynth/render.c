// Plays a MIDI file with a SoundFont through the library's MIDI synthesizer
// (hps/mister_midi.c) and the C model of the core's voice engine
// (midisynth.c) into a WAV file, without a core: for comparing with
// FluidSynth, and for listening.
//
// build: gcc -O2 -o render tools/midisynth/render.c tools/midisynth/midisynth.c
//            tools/midisynth/smf.c hps/mister_midi.c -lm
// usage: render <font.sf2> <song.mid> <out.wav> [volume]
//   volume: the core's MIDI volume, 32 as it comes (MH_MidiVolume())
//   MIDISYNTH_FLUID=1 in the environment times as FluidSynth's player does
//   (fluid_player_callback(): whole milliseconds at the start of each block
//   of 64 frames, track by track), every voice's control update at those
//   blocks too. Not what the core does: for comparing sample by sample.
//   MIDISYNTH_FX=0: without the reverb and the chorus (FluidSynth's -R 0 -C 0).
// It prints the most voices playing at once, the voices taken from notes
// still playing for new ones, the commands sent and the samples held at
// the 16-bit limits.
#include "midisynth.h"
#include "smf.h"
#include "../../hps/mister_midi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    midisynth* ms;
    long commands, updates, starts, steals, dropped;
} ctx_t;

static void emit(void* p, const uint64_t* cmd) {
    ctx_t* c = p;
    int command = (int)((cmd[0] >> 40) & 0xff), voice = (int)((cmd[0] >> 32) & 127);
    uint32_t bits[4];

    c->commands++;
    if (command == MH_MIDI_UPDATE) {
        c->updates++;
    }
    if (command == MH_MIDI_START) {
        c->starts++;
        ms_playing(c->ms, bits);
        if (bits[voice >> 5] >> (voice & 31) & 1) {
            c->steals++;
        }
    }
    if (!ms_push(c->ms, cmd)) {
        c->dropped++;
    }
}

static void send(mh_midi* m, uint32_t frame, const smf_event* ev) {
    if (ev->status == 0xf0) {
        mh_midi_sysex(m, frame, ev->data, ev->length);
    } else if (ev->status != 0xff) {
        mh_midi_event(m, frame, ev->status, ev->data1, ev->data2);
    }
}

// FluidSynth's player at the block starting at `frame`
typedef struct {
    int* next;           // each track's next event in the song's list
    long start_msec;
    int start_ticks;
    float deltatime;     // milliseconds a tick, a float as in FluidSynth
    int ended;
} fluid_player;

static void fluid_block(fluid_player* pl, const smf_song* song, mh_midi* m, uint32_t frame) {
    long msec = (long)(1000.0 * frame / MH_MIDI_RATE);
    int cur_ticks = pl->start_ticks + (int)((double)(msec - pl->start_msec) / pl->deltatime + 0.5), t;

    for (t = 0; t < song->tracks; t++) {
        int* e = &pl->next[t];

        for (; *e < song->count; ++*e) {
            const smf_event* ev = &song->events[*e];

            if (ev->track != t) {
                continue;
            }
            if (ev->tick > (uint32_t)cur_ticks) {
                break;
            }
            if (ev->status == 0xff) {
                pl->deltatime = (float)ev->tempo / (float)song->division / (float)1000.0;
                pl->start_msec = msec;
                pl->start_ticks = cur_ticks;
            }
            send(m, frame, ev);
        }
    }
    // at the end of the file the player lets go of the pedals and of the
    // notes still on
    if (!pl->ended && (uint32_t)cur_ticks >= song->end_tick) {
        pl->ended = 1;
        for (t = 0; t < 16; t++) {
            mh_midi_event(m, frame, 0xb0 | t, 64, 0);
            mh_midi_event(m, frame, 0xb0 | t, 66, 0);
            mh_midi_event(m, frame, 0xb0 | t, 123, 0);
        }
    }
}

static void put32(FILE* f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };

    fwrite(b, 1, 4, f);
}

static void wav_header(FILE* f, uint32_t frames) {
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + frames * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put32(f, 1 | 2 << 16);
    put32(f, MH_MIDI_RATE);
    put32(f, MH_MIDI_RATE * 4);
    put32(f, 4 | 16 << 16);
    fwrite("data", 1, 4, f);
    put32(f, frames * 4);
}

int main(int argc, char** argv) {
    static int16_t samples[MH_MIDI_MAX_SAMPLES];
    char err[200];
    mh_sfont* font;
    mh_midi* m;
    smf_song song;
    ctx_t c;
    FILE* out;
    uint32_t frame = 0, end, bits[4];
    int volume = argc > 4 ? atoi(argv[4]) : 32, e = 0, most = 0, n;
    long clipped = 0;
    double sum2 = 0;
    int peak = 0;
    int fluid = getenv("MIDISYNTH_FLUID") != NULL && atoi(getenv("MIDISYNTH_FLUID")) != 0;
    int fx = getenv("MIDISYNTH_FX") == NULL || atoi(getenv("MIDISYNTH_FX")) != 0;
    fluid_player pl;

    if (argc < 4) {
        fprintf(stderr, "usage: render <font.sf2> <song.mid> <out.wav> [volume]\n");
        return 1;
    }
    font = mh_sfont_load(argv[1], samples, MH_MIDI_MAX_SAMPLES, err, sizeof(err));
    if (font == NULL) {
        fprintf(stderr, "%s %s\n", argv[1], err);
        return 1;
    }
    if (!smf_load(argv[2], &song, err, sizeof(err))) {
        fprintf(stderr, "%s %s\n", argv[2], err);
        return 1;
    }
    memset(&c, 0, sizeof(c));
    c.ms = ms_new(samples, MH_MIDI_MAX_SAMPLES);
    ms_aligned(c.ms, fluid);
    m = mh_midi_new(emit, &c);
    mh_midi_set_font(m, font);
    out = fopen(argv[3], "wb");
    if (out == NULL) {
        fprintf(stderr, "%s cannot be written\n", argv[3]);
        return 1;
    }
    memset(&pl, 0, sizeof(pl));
    pl.next = calloc((size_t)song.tracks + 1, sizeof(int));
    pl.deltatime = (float)500000 / (float)song.division / (float)1000.0;
    end = (uint32_t)(song.length * MH_MIDI_RATE) + 3 * MH_MIDI_RATE;
    wav_header(out, end);
    for (frame = 0; frame < end; frame++) {
        int64_t sums[4];
        int16_t lr[2];

        if (fluid) {
            if (frame % MH_MIDI_BLOCK == 0) {
                fluid_block(&pl, &song, m, frame);
            }
        } else {
            while (e < song.count && (uint32_t)(song.events[e].seconds * MH_MIDI_RATE + 0.5) <= frame) {
                send(m, frame, &song.events[e++]);
            }
        }
        mh_midi_flush(m);
        // the engine reports its voices at every vblank
        if (frame % (fluid ? MH_MIDI_BLOCK : 735) == 0) {
            ms_playing(c.ms, bits);
            mh_midi_voices(m, frame, bits);
        }
        ms_frame(c.ms, frame, sums);
        if (fx) {
            ms_effects(c.ms, sums);
        }
        ms_output(sums, volume, lr);
        for (n = 0; n < 2; n++) {
            int a = lr[n] < 0 ? -lr[n] : lr[n];

            clipped += a >= 32767;
            peak = a > peak ? a : peak;
            sum2 += (double)lr[n] * lr[n];
        }
        n = ms_playing(c.ms, bits);
        most = n > most ? n : most;
        fwrite(lr, 2, 2, out);
    }
    fclose(out);
    free(pl.next);
    printf("%s: %.1f s, voices at most %d, notes cut for others %ld of %ld, commands %ld (updates %ld, dropped %ld), "
           "peak %d, rms %.1f, held at the limits %ld\n",
        argv[2], end / (double)MH_MIDI_RATE, most, c.steals, c.starts, c.commands, c.updates, c.dropped, peak,
        sqrt(sum2 / (2.0 * end)), clipped);
    return 0;
}
