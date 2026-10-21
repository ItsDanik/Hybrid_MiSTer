// The MIDI synthesizer's ARM side, inside the library (docs/midi_synth.md):
// a SoundFont's presets, the MIDI channels and the 128 voices, which become
// commands for the core's voice engine. Not for games: they use MH_Midi*()
// of mister_hybrid.h. The C model of the engine (tools/midisynth/) takes the
// commands' layout from here.
#ifndef MISTER_MIDI_H
#define MISTER_MIDI_H

#include <stdint.h>

#define MH_MIDI_VOICES 128
#define MH_MIDI_BLOCK 64           // frames between two control updates of a voice
#define MH_MIDI_RING_CMDS 1024     // commands in the ring at 0x030000
#define MH_MIDI_CMD_WORDS 16       // a command is 16 words of 64 bits
#define MH_MIDI_MAX_SAMPLES (32u << 20) // 64 MB of 16-bit samples
#define MH_MIDI_RATE 44100

enum {
    MH_MIDI_START = 1,
    MH_MIDI_UPDATE = 2,
    MH_MIDI_RELEASE = 3,
    MH_MIDI_KILL = 4,
    MH_MIDI_STOP = 5,
    MH_MIDI_GLOBAL = 6,
};

// A command's parameter block, unpacked (docs/midi_synth.md, "The parameter
// block"). Envelope values x are Q1.31 in 0..1, LFO values Q1.31 in -1..1;
// increments are of x a block.
typedef struct {
    uint32_t start, end;            // sample numbers; end is the first after the last
    uint32_t loop_start, loop_end;  // loop_end is the first after the loop
    uint32_t loop_mode;             // 0 none, 1 loop, 3 loop until released (2 is none)
    uint32_t looped;                // the start is inside the loop, the voice has looped
    int32_t pitch;                  // U: the key's pitch with the tunings, cents Q.15
    int32_t root;                   // the sample's root key with its correction, cents
    uint64_t rate;                  // the sample's rate against the output's, Q4.32
    uint32_t atten;                 // U: 1/16 cB
    uint32_t end_level[2];          // the volume envelope's x below which the voice
                                    // ends, before and after it has looped
    uint32_t gain_l, gain_r;        // U: Q0.16
    uint32_t gain_rev, gain_cho;    // U: Q0.16
    uint32_t fc;                    // U: cutoff, absolute cents 1500..13500, Q.8
    uint32_t q;                     // U: cB 0..960
    int32_t modlfo_pitch, viblfo_pitch, modenv_pitch; // U: cents, Q.8
    int32_t modlfo_fc, modenv_fc;   // U: cents, Q.8
    int32_t modlfo_vol;             // U: cB, Q.8, positive is louder
    struct {
        uint32_t delay, attack, hold;    // blocks, increment Q1.31, blocks
        uint64_t decay;                  // decrement Q1.39
        uint32_t sustain;                // level Q1.31
        uint64_t release;                // decrement Q1.39
    } env[2];                       // 0 volume, 1 modulation (its decay and release
                                    // Q1.31, the low 8 bits 0)
    struct {
        uint32_t delay, incr;       // blocks, increment of the triangle (-1..1) a block, Q1.31
    } lfo[2];                       // 0 modulation, 1 vibrato
} mh_midi_params;

static inline uint64_t mh_midi_bits(uint64_t v, int lo, int n) {
    return (v & ((1ull << n) - 1)) << lo;
}

static inline uint64_t mh_midi_field(uint64_t w, int lo, int n) {
    return (w >> lo) & ((1ull << n) - 1);
}

static inline void mh_midi_pack(uint64_t* w, uint32_t frame, int voice, int command, const mh_midi_params* p) {
    int i;

    for (i = 0; i < MH_MIDI_CMD_WORDS; i++) {
        w[i] = 0;
    }
    w[0] = frame | mh_midi_bits(voice, 32, 7) | mh_midi_bits(command, 40, 8);
    if (p == 0) {
        return;
    }
    w[1] = mh_midi_bits(p->start, 0, 25) | mh_midi_bits(p->end, 25, 25) | mh_midi_bits(p->loop_mode, 50, 2)
        | mh_midi_bits(p->q, 52, 10) | mh_midi_bits(p->looped, 62, 1);
    w[2] = mh_midi_bits(p->loop_start, 0, 25) | mh_midi_bits(p->loop_end, 25, 25);
    w[3] = mh_midi_bits((uint32_t)p->pitch, 0, 32) | mh_midi_bits(p->atten, 32, 16)
        | mh_midi_bits((uint32_t)p->root, 48, 16);
    w[4] = mh_midi_bits(p->gain_l, 0, 16) | mh_midi_bits(p->gain_r, 16, 16) | mh_midi_bits(p->gain_rev, 32, 16)
        | mh_midi_bits(p->gain_cho, 48, 16);
    w[5] = mh_midi_bits(p->fc, 0, 22) | mh_midi_bits(p->rate, 22, 36);
    w[6] = mh_midi_bits((uint32_t)p->modlfo_pitch, 0, 24) | mh_midi_bits((uint32_t)p->viblfo_pitch, 24, 24)
        | mh_midi_bits(p->lfo[0].delay, 48, 16);
    w[7] = mh_midi_bits((uint32_t)p->modenv_pitch, 0, 24) | mh_midi_bits((uint32_t)p->modlfo_fc, 24, 24)
        | mh_midi_bits(p->lfo[1].delay, 48, 16);
    w[8] = mh_midi_bits((uint32_t)p->modenv_fc, 0, 24) | mh_midi_bits((uint32_t)p->modlfo_vol, 24, 24)
        | mh_midi_bits(p->env[0].decay & 0xff, 48, 8) | mh_midi_bits(p->env[0].release & 0xff, 56, 8);
    for (i = 0; i < 2; i++) {
        w[9 + 2 * i] = mh_midi_bits(p->env[i].attack, 0, 32) | mh_midi_bits(p->env[i].decay >> 8, 32, 32);
        w[10 + 2 * i] = mh_midi_bits(p->env[i].sustain, 0, 32) | mh_midi_bits(p->env[i].release >> 8, 32, 32);
        w[13] |= mh_midi_bits(p->env[i].delay, 32 * i, 16) | mh_midi_bits(p->env[i].hold, 32 * i + 16, 16);
        w[14] |= mh_midi_bits(p->end_level[i], 32 * i, 32);
        w[15] |= mh_midi_bits(p->lfo[i].incr, 32 * i, 32);
    }
}

static inline int32_t mh_midi_s16(uint64_t w, int lo) {
    return (int16_t)mh_midi_field(w, lo, 16);
}

static inline int32_t mh_midi_s24(uint64_t w, int lo) {
    return (int32_t)((uint32_t)mh_midi_field(w, lo, 24) << 8) >> 8;
}

static inline void mh_midi_unpack(const uint64_t* w, uint32_t* frame, int* voice, int* command, mh_midi_params* p) {
    int i;

    *frame = (uint32_t)w[0];
    *voice = (int)mh_midi_field(w[0], 32, 7);
    *command = (int)mh_midi_field(w[0], 40, 8);
    p->start = mh_midi_field(w[1], 0, 25);
    p->end = mh_midi_field(w[1], 25, 25);
    p->loop_mode = mh_midi_field(w[1], 50, 2);
    p->looped = mh_midi_field(w[1], 62, 1);
    p->loop_start = mh_midi_field(w[2], 0, 25);
    p->loop_end = mh_midi_field(w[2], 25, 25);
    p->pitch = (int32_t)mh_midi_field(w[3], 0, 32);
    p->atten = mh_midi_field(w[3], 32, 16);
    p->root = mh_midi_s16(w[3], 48);
    p->gain_l = mh_midi_field(w[4], 0, 16);
    p->gain_r = mh_midi_field(w[4], 16, 16);
    p->gain_rev = mh_midi_field(w[4], 32, 16);
    p->gain_cho = mh_midi_field(w[4], 48, 16);
    p->fc = mh_midi_field(w[5], 0, 22);
    p->q = mh_midi_field(w[1], 52, 10);
    p->rate = mh_midi_field(w[5], 22, 36);
    p->modlfo_pitch = mh_midi_s24(w[6], 0);
    p->viblfo_pitch = mh_midi_s24(w[6], 24);
    p->lfo[0].delay = mh_midi_field(w[6], 48, 16);
    p->modenv_pitch = mh_midi_s24(w[7], 0);
    p->modlfo_fc = mh_midi_s24(w[7], 24);
    p->lfo[1].delay = mh_midi_field(w[7], 48, 16);
    p->modenv_fc = mh_midi_s24(w[8], 0);
    p->modlfo_vol = mh_midi_s24(w[8], 24);
    for (i = 0; i < 2; i++) {
        p->env[i].attack = mh_midi_field(w[9 + 2 * i], 0, 32);
        p->env[i].decay = mh_midi_field(w[9 + 2 * i], 32, 32) << 8 | (i == 0 ? mh_midi_field(w[8], 48, 8) : 0);
        p->env[i].sustain = mh_midi_field(w[10 + 2 * i], 0, 32);
        p->env[i].release = mh_midi_field(w[10 + 2 * i], 32, 32) << 8 | (i == 0 ? mh_midi_field(w[8], 56, 8) : 0);
        p->env[i].delay = mh_midi_field(w[13], 32 * i, 16);
        p->env[i].hold = mh_midi_field(w[13], 32 * i + 16, 16);
        p->end_level[i] = mh_midi_field(w[14], 32 * i, 32);
        p->lfo[i].incr = mh_midi_field(w[15], 32 * i, 32);
    }
}

// The ARM side (mister_midi.c). Every call takes the audio frame it is for;
// frames must not go back. Commands come out through the emit function, in
// the order of their frames, when a later block of frames is reached or on
// mh_midi_flush().
typedef struct mh_sfont mh_sfont;
typedef struct mh_midi mh_midi;
typedef void (*mh_midi_emit)(void* ctx, const uint64_t* cmd);

// Reads a SoundFont 2: its presets, and its samples into samples[] (at
// most max_samples). NULL on failure, with the reason in err.
mh_sfont* mh_sfont_load(const char* path, int16_t* samples, uint32_t max_samples, char* err, int errlen);
void mh_sfont_free(mh_sfont* font);

mh_midi* mh_midi_new(mh_midi_emit emit, void* ctx);
void mh_midi_free(mh_midi* m);
// The font notes are made with (NULL: none, note-ons are ignored). The
// channels keep their programs; the voices must have been stopped.
void mh_midi_set_font(mh_midi* m, mh_sfont* font);
// The output gain the engine's mix has (0.5 as FluidSynth's synth.gain): only
// for the level at which a voice is inaudible.
void mh_midi_set_gain(mh_midi* m, float gain);
void mh_midi_event(mh_midi* m, uint32_t frame, int status, int data1, int data2);
void mh_midi_sysex(mh_midi* m, uint32_t frame, const uint8_t* data, int length); // without F0 and F7
void mh_midi_reset(mh_midi* m, uint32_t frame);     // GM system reset: voices stopped, channels as at the start
void mh_midi_stop_all(mh_midi* m, uint32_t frame);  // every voice silent at once
// What the engine reported: the voices playing at frame (bit v of
// playing[v / 32]). Voices that ended become free.
void mh_midi_voices(mh_midi* m, uint32_t frame, const uint32_t* playing);
void mh_midi_flush(mh_midi* m);
int mh_midi_busy(const mh_midi* m); // the voices the ARM side holds as playing

#endif
