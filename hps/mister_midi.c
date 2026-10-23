// SPDX-License-Identifier: GPL-2.0-or-later
// Part of Hybrid_MiSTer (hps/LICENSE). Follows FluidSynth (LGPL-2.1-or-later),
// taken here under the GPL as its section 3 allows.
//
// The MIDI synthesizer's ARM side: see mister_midi.h and docs/midi_synth.md.
// What a note sounds like is FluidSynth 2.6.1's: its SoundFont loader, its
// generators and modulators, its default modulators, its voice allocation;
// the functions it comes from are named where it matters. The core's voice
// engine does the rest, from the commands this sends.
#include "mister_midi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SoundFont 2 generators, and two of FluidSynth's own after them
enum {
    G_START = 0, G_END = 1, G_LSTART = 2, G_LEND = 3, G_STARTC = 4,
    G_MLFO_PITCH = 5, G_VLFO_PITCH = 6, G_MENV_PITCH = 7, G_FC = 8, G_Q = 9,
    G_MLFO_FC = 10, G_MENV_FC = 11, G_ENDC = 12, G_MLFO_VOL = 13,
    G_CHORUS = 15, G_REVERB = 16, G_PAN = 17,
    G_MLFO_DELAY = 21, G_MLFO_FREQ = 22, G_VLFO_DELAY = 23, G_VLFO_FREQ = 24,
    G_MENV_DELAY = 25, G_MENV_ATTACK = 26, G_MENV_HOLD = 27, G_MENV_DECAY = 28,
    G_MENV_SUSTAIN = 29, G_MENV_RELEASE = 30, G_KEY2MENV_HOLD = 31, G_KEY2MENV_DECAY = 32,
    G_VENV_DELAY = 33, G_VENV_ATTACK = 34, G_VENV_HOLD = 35, G_VENV_DECAY = 36,
    G_VENV_SUSTAIN = 37, G_VENV_RELEASE = 38, G_KEY2VENV_HOLD = 39, G_KEY2VENV_DECAY = 40,
    G_INSTRUMENT = 41, G_KEYRANGE = 43, G_VELRANGE = 44, G_LSTARTC = 45,
    G_KEYNUM = 46, G_VELOCITY = 47, G_ATTEN = 48, G_LENDC = 50,
    G_COARSE = 51, G_FINE = 52, G_SAMPLEID = 53, G_MODE = 54, G_SCALE = 56,
    G_EXCL = 57, G_ROOT = 58,
    G_PITCH = 59,   // FluidSynth's GEN_PITCH: the key's pitch in cents
    G_BALANCE = 60, // FluidSynth's GEN_CUSTOM_BALANCE
    NGEN = 61
};

// modulator sources (SF2 section 8.2): index, and the flags of the word
#define SRC_CC 0x80
#define SRC_NEG 0x100
#define SRC_BIPOLAR 0x200
#define SRC_TYPE(s) (((s) >> 10) & 63)
enum { S_NONE = 0, S_VEL = 2, S_KEY = 3, S_POLY = 10, S_CHPRESS = 13, S_BEND = 14, S_BENDSENS = 16 };

#define MAX_MODS 64     // FluidSynth's FLUID_NUM_MOD
#define PEAK_ATT 960.0f
#define NOISE_FLOOR 2e-7f
#define QUEUE_MAX 256
#define KILL_BLOCKS 218    // the engine's release of a KILL (MS_KILL_DEC)

typedef struct {
    uint16_t src1, src2, dest, trans;
    float amount;
} mod_t;

typedef struct {
    uint8_t keylo, keyhi, vello, velhi;
    uint64_t set;           // generators set in the zone
    float gen[NGEN];
    int mod0, nmods;        // in the font's mods[]
    int link;               // instrument (preset zone) or sample (instrument zone)
} zone_t;

typedef struct {
    int global;             // zone index or -1
    int zone0, nzones;
} zlist_t;

typedef struct {
    char name[21];
    int bank, prog;
    zlist_t z;
} preset_t;

typedef struct {
    uint32_t start, end;            // end: the last sample (FluidSynth's)
    uint32_t loop_start, loop_end;  // loop_end: the first after the loop
    uint32_t rate;
    int root, adj;
    float noise;                    // FluidSynth's amplitude_that_reaches_noise_floor
    int ok;
} sample_t;

struct mh_sfont {
    preset_t* presets;
    int npresets;
    zlist_t* insts;
    int ninsts;
    zone_t* zones;
    int nzones;
    mod_t* mods;
    int nmods;
    sample_t* samples;
    int nsamples;
};

typedef struct {
    uint8_t cc[128];
    uint8_t key_pressure[128];
    int channel_pressure, pitch_bend;
    float bend_range, mod_depth;
    float fine, coarse;             // RPN tuning (FluidSynth's channel generators)
    int drum;
    int bank, prog;
    int preset;                     // index in the font, -1: none
    uint32_t sostenuto_id;
    int nrpn_active;
} chan_t;

enum { V_ON, V_SUSTAINED, V_SOSTENUTO };

typedef struct {
    int busy;                       // the engine may be playing it
    int chan, key, vel;
    int state;
    int released;                   // FluidSynth's has_noteoff
    uint32_t id;
    uint32_t start_frame;
    int ends_known;                 // free_after holds
    uint32_t free_after;            // the engine has surely ended it by this frame
    const sample_t* s;
    float gen[NGEN];
    double mod[NGEN];               // in double, as FluidSynth's: a pitch bend's fraction of a cent
    mod_t mods[MAX_MODS];
    int nmods;
    int excl;
    mh_midi_params sent;
    // taken from a note still playing at this frame: FluidSynth's voice then
    // plays on its overflow rvoice, and cannot be taken again until the one
    // it cut is over, at the end of that block
    int stolen;
    uint32_t stolen_at;
} voice_t;

typedef struct {
    uint32_t frame;
    int voice, cmd;
    mh_midi_params p;
} pending_t;

enum { STYLE_GS, STYLE_GM, STYLE_GM2, STYLE_XG };

struct mh_midi {
    mh_midi_emit emit;
    void* ctx;
    mh_sfont* font;
    float gain;
    int style;
    int started;
    uint32_t now;
    uint32_t noteid;
    chan_t ch[16];
    voice_t v[MH_MIDI_VOICES];
    pending_t queue[QUEUE_MAX];
    int nq;
};

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

//////////////////////////////////////////////////////////////////
// The SoundFont

static const float gen_default[NGEN] = {
    [G_FC] = 13500, [G_MLFO_DELAY] = -12000, [G_VLFO_DELAY] = -12000,
    [G_MENV_DELAY] = -12000, [G_MENV_ATTACK] = -12000, [G_MENV_HOLD] = -12000,
    [G_MENV_DECAY] = -12000, [G_MENV_RELEASE] = -12000,
    [G_VENV_DELAY] = -12000, [G_VENV_ATTACK] = -12000, [G_VENV_HOLD] = -12000,
    [G_VENV_DECAY] = -12000, [G_VENV_RELEASE] = -12000,
    [G_KEYNUM] = -1, [G_VELOCITY] = -1, [G_SCALE] = 100, [G_ROOT] = -1,
};

// generators a preset zone may not have (SF2 section 8.5)
static int preset_gen_ok(int g) {
    switch (g) {
    case G_START: case G_END: case G_LSTART: case G_LEND: case G_STARTC: case G_ENDC:
    case G_LSTARTC: case G_LENDC: case G_KEYNUM: case G_VELOCITY: case G_MODE:
    case G_EXCL: case G_ROOT: case G_SAMPLEID:
        return 0;
    default:
        return g < G_PITCH && g != 14 && g != 18 && g != 19 && g != 20 && g != 42 && g != 49 && g != 55;
    }
}

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// FluidSynth's fluid_mod_check_sources()
static int source_ok(uint16_t s, int is_src1) {
    int i = s & 127;

    (void)is_src1;
    if (SRC_TYPE(s) > 3) {
        return 0;
    }
    if (s & SRC_CC) {
        return i != 0 && i != 6 && i != 32 && i != 38 && (i < 98 || i > 101) && i < 120;
    }
    return i == S_NONE || i == S_VEL || i == S_KEY || i == S_POLY || i == S_CHPRESS || i == S_BEND || i == S_BENDSENS;
}

static int mod_valid(const mod_t* m) {
    if (!source_ok(m->src1, 1)) {
        return 0;
    }
    if (!(m->src1 & SRC_CC) && (m->src1 & 127) == S_NONE) {
        return 1;
    }
    return source_ok(m->src2, 0) && !(m->dest & 0x8000) && m->dest < G_PITCH;
}

static int mod_same(const mod_t* a, const mod_t* b) {
    return a->dest == b->dest && a->src1 == b->src1 && a->src2 == b->src2;
}

typedef struct {
    const uint8_t *hdr, *bag, *mod, *gen;
    uint32_t nhdr, nbag, nmod, ngen;
    int hdr_size;
    int preset;
} pdta_t;

// The zones of one preset or instrument (FluidSynth's fluid_sffile.c and
// fluid_defsfont.c): a first zone without its instrument / sample generator
// is the global one; generators after that one are ignored; a zone's
// identical modulators keep the last one.
static int load_zones(mh_sfont* f, const pdta_t* t, int index, zlist_t* zl) {
    int bag0 = rd16(t->hdr + index * t->hdr_size + t->hdr_size - (t->preset ? 14 : 2));
    int bag1 = rd16(t->hdr + (index + 1) * t->hdr_size + t->hdr_size - (t->preset ? 14 : 2));
    const int term = t->preset ? G_INSTRUMENT : G_SAMPLEID;
    int b, k, j;

    zl->global = -1;
    zl->zone0 = f->nzones;
    zl->nzones = 0;
    if (bag1 < bag0 || (uint32_t)bag1 >= t->nbag) {
        return 0;
    }
    for (b = bag0; b < bag1; b++) {
        int g0 = rd16(t->bag + b * 4), g1 = rd16(t->bag + (b + 1) * 4);
        int m0 = rd16(t->bag + b * 4 + 2), m1 = rd16(t->bag + (b + 1) * 4 + 2);
        zone_t z;
        int has_term = 0, n;

        memset(&z, 0, sizeof(z));
        z.keyhi = z.velhi = 127;
        z.link = -1;
        if (g1 < g0 || (uint32_t)g1 >= t->ngen || m1 < m0 || (uint32_t)m1 >= t->nmod) {
            return 0;
        }
        for (k = g0; k < g1 && !has_term; k++) {
            int op = rd16(t->gen + k * 4);
            const uint8_t* a = t->gen + k * 4 + 2;
            int16_t amount = (int16_t)rd16(a);

            if (op == G_KEYRANGE) {
                z.keylo = a[0];
                z.keyhi = a[1];
                z.set |= 1ull << G_KEYRANGE;
            } else if (op == G_VELRANGE) {
                z.vello = a[0];
                z.velhi = a[1];
                z.set |= 1ull << G_VELRANGE;
            } else if (op == term) {
                z.link = rd16(a);
                has_term = 1;
            } else if (op < G_PITCH && (!t->preset || preset_gen_ok(op)) && op != G_INSTRUMENT && op != G_SAMPLEID) {
                // EMU's factor of the attenuation (FluidSynth's EMU_ATTENUATION_FACTOR)
                z.gen[op] = op == G_ATTEN ? amount * 0.4f : amount;
                z.set |= 1ull << op;
            }
        }
        if (!has_term) {
            if (b != bag0) {
                continue; // a global zone that is not the first is ignored
            }
        } else if (zl->global >= 0) {
            // a local zone without key or velocity range has the global one's (FluidSynth #1250)
            const zone_t* gz = &f->zones[zl->global];

            if (!(z.set & (1ull << G_KEYRANGE))) {
                z.keylo = gz->keylo;
                z.keyhi = gz->keyhi;
            }
            if (!(z.set & (1ull << G_VELRANGE))) {
                z.vello = gz->vello;
                z.velhi = gz->velhi;
            }
        }
        z.mod0 = f->nmods;
        for (k = m0; k < m1; k++) {
            const uint8_t* p = t->mod + k * 10;
            mod_t m;

            m.src1 = rd16(p);
            m.dest = rd16(p + 2);
            m.amount = (int16_t)rd16(p + 4);
            m.src2 = rd16(p + 6);
            m.trans = rd16(p + 8);
            if (m.trans != 0 && m.trans != 2) {
                m.amount = 0;
                m.trans = 0;
            }
            if (SRC_TYPE(m.src1) > 3 || SRC_TYPE(m.src2) > 3) {
                m.amount = 0;
            }
            if (!mod_valid(&m)) {
                continue;
            }
            f->mods[f->nmods++] = m;
        }
        // of identical modulators the last one stays
        n = f->nmods - z.mod0;
        for (k = 0; k < n; k++) {
            for (j = k + 1; j < n; j++) {
                if (mod_same(&f->mods[z.mod0 + k], &f->mods[z.mod0 + j])) {
                    memmove(&f->mods[z.mod0 + k], &f->mods[z.mod0 + k + 1], (size_t)(n - k - 1) * sizeof(mod_t));
                    n--;
                    k--;
                    break;
                }
            }
        }
        if (n > MAX_MODS) {
            n = MAX_MODS;
        }
        f->nmods = z.mod0 + n;
        z.nmods = n;
        if (!has_term) {
            zl->global = f->nzones;
        } else {
            zl->nzones++;
        }
        f->zones[f->nzones++] = z;
        if (!has_term) {
            // keep the local zones together: the global one moves to the front
            zl->zone0 = f->nzones;
        }
    }
    return 1;
}

static int find_chunk(FILE* fp, long from, long to, const char* id, const char* list, long* at, uint32_t* size) {
    uint8_t h[12];
    long pos = from;

    while (pos + 8 <= to) {
        uint32_t sz;

        if (fseek(fp, pos, SEEK_SET) != 0 || fread(h, 1, 12, fp) < 8) {
            return 0;
        }
        sz = rd32(h + 4);
        if (memcmp(h, id, 4) == 0 && (list == NULL || memcmp(h + 8, list, 4) == 0)) {
            *at = pos + 8;
            *size = sz;
            return 1;
        }
        pos += 8 + (long)sz + (sz & 1);
    }
    return 0;
}

static uint8_t* read_at(FILE* fp, long at, uint32_t size) {
    uint8_t* p = malloc(size ? size : 1);

    if (p != NULL && (fseek(fp, at, SEEK_SET) != 0 || fread(p, 1, size, fp) != size)) {
        free(p);
        p = NULL;
    }
    return p;
}

void mh_sfont_free(mh_sfont* f) {
    if (f != NULL) {
        free(f->presets);
        free(f->insts);
        free(f->zones);
        free(f->mods);
        free(f->samples);
        free(f);
    }
}

mh_sfont* mh_sfont_load(const char* path, int16_t* samples, uint32_t max_samples, char* err, int errlen) {
    static const char* names[9] = { "phdr", "pbag", "pmod", "pgen", "inst", "ibag", "imod", "igen", "shdr" };
    static const int sizes[9] = { 38, 4, 10, 4, 22, 4, 10, 4, 46 };
    FILE* fp;
    long end, sdta, pdta, smpl, at;
    uint32_t sz, sdta_size, pdta_size, smpl_size, n, i;
    uint8_t* chunk[9] = { 0 };
    uint32_t count[9];
    uint8_t riff[12];
    mh_sfont* f = NULL;
    int k, ok = 0;
    int16_t* buf = NULL;

    err[0] = 0;
    fp = fopen(path, "rb");
    if (fp == NULL) {
        snprintf(err, (size_t)errlen, "cannot be read");
        return NULL;
    }
    if (fread(riff, 1, 12, fp) != 12 || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "sfbk", 4) != 0) {
        snprintf(err, (size_t)errlen, "is no SoundFont 2");
        goto done;
    }
    end = 8 + (long)rd32(riff + 4);
    if (!find_chunk(fp, 12, end, "LIST", "sdta", &sdta, &sdta_size)
        || !find_chunk(fp, 12, end, "LIST", "pdta", &pdta, &pdta_size)
        || !find_chunk(fp, sdta + 4, sdta + (long)sdta_size, "smpl", NULL, &smpl, &smpl_size)) {
        snprintf(err, (size_t)errlen, "is no SoundFont 2");
        goto done;
    }
    if (smpl_size / 2 > max_samples) {
        snprintf(err, (size_t)errlen, "has %u MB of samples, more than the %u MB there is room for",
            (unsigned)((smpl_size + (1u << 20) - 1) >> 20), (unsigned)(max_samples >> 19));
        goto done;
    }
    // the headers (phdr, inst, shdr) have one at least and the terminal
    // record, the rest the terminal record at least (a font without
    // modulators has only that: AWE64.sf2)
    for (k = 0; k < 9; k++) {
        if (!find_chunk(fp, pdta + 4, pdta + (long)pdta_size, names[k], NULL, &at, &sz)
            || sz % (uint32_t)sizes[k] != 0 || sz / (uint32_t)sizes[k] < (k % 4 == 0 ? 2u : 1u)
            || (chunk[k] = read_at(fp, at, sz)) == NULL) {
            snprintf(err, (size_t)errlen, "is damaged (%s)", names[k]);
            goto done;
        }
        count[k] = sz / (uint32_t)sizes[k];
    }

    f = calloc(1, sizeof(*f));
    if (f == NULL) {
        goto nomem;
    }
    f->npresets = (int)count[0] - 1;
    f->ninsts = (int)count[4] - 1;
    f->nsamples = (int)count[8] - 1;
    f->presets = calloc((size_t)f->npresets, sizeof(preset_t));
    f->insts = calloc((size_t)f->ninsts, sizeof(zlist_t));
    f->zones = calloc(count[1] + count[5], sizeof(zone_t));
    f->mods = calloc(count[2] + count[6] + 1, sizeof(mod_t));
    f->samples = calloc((size_t)f->nsamples, sizeof(sample_t));
    if (f->presets == NULL || f->insts == NULL || f->zones == NULL || f->mods == NULL || f->samples == NULL) {
        goto nomem;
    }

    for (k = 0; k < f->nsamples; k++) {
        const uint8_t* p = chunk[8] + k * 46;
        sample_t* s = &f->samples[k];
        uint32_t st = rd32(p + 20), en = rd32(p + 24);

        s->start = st;
        s->end = en > 0 ? en - 1 : 0; // FluidSynth: the last sample, "contrary to SF spec"
        s->loop_start = rd32(p + 28);
        s->loop_end = rd32(p + 32);
        s->rate = rd32(p + 36);
        s->root = p[40];
        s->adj = (int8_t)p[41];
        s->ok = !(rd16(p + 44) & 0x8000) && en > st && en <= smpl_size / 2 && s->rate > 0;
        if (s->root > 127) {
            s->root = 60;
        }
    }
    for (k = 0; k < f->ninsts; k++) {
        pdta_t t = { chunk[4], chunk[5], chunk[6], chunk[7], count[4], count[5], count[6], count[7], 22, 0 };

        if (!load_zones(f, &t, k, &f->insts[k])) {
            snprintf(err, (size_t)errlen, "is damaged (instrument %d)", k);
            goto done;
        }
    }
    for (k = 0; k < f->npresets; k++) {
        pdta_t t = { chunk[0], chunk[1], chunk[2], chunk[3], count[0], count[1], count[2], count[3], 38, 1 };
        preset_t* pr = &f->presets[k];

        memcpy(pr->name, chunk[0] + k * 38, 20);
        pr->prog = rd16(chunk[0] + k * 38 + 20);
        pr->bank = rd16(chunk[0] + k * 38 + 22);
        if (!load_zones(f, &t, k, &pr->z)) {
            snprintf(err, (size_t)errlen, "is damaged (preset %d)", k);
            goto done;
        }
    }

    // the samples, and the loudest point of each loop on the way
    // (FluidSynth's fluid_voice_optimize_sample())
    {
        enum { CHUNK = 128 * 1024 };
        int32_t* peak = calloc((size_t)f->nsamples, sizeof(int32_t));

        buf = malloc(CHUNK * sizeof(int16_t));
        if (peak == NULL || buf == NULL || fseek(fp, smpl, SEEK_SET) != 0) {
            free(peak);
            goto nomem;
        }
        n = smpl_size / 2;
        for (i = 0; i < n; i += CHUNK) {
            uint32_t len = n - i < CHUNK ? n - i : CHUNK;

            if (fread(buf, 2, len, fp) != len) {
                free(peak);
                snprintf(err, (size_t)errlen, "is damaged (samples)");
                goto done;
            }
            memcpy(samples + i, buf, len * sizeof(int16_t));
            for (k = 0; k < f->nsamples; k++) {
                const sample_t* s = &f->samples[k];
                uint32_t a = s->loop_start > i ? s->loop_start : i;
                uint32_t b = s->loop_end < i + len ? s->loop_end : i + len;

                for (; a < b; a++) {
                    int32_t v = buf[a - i];

                    v = v < 0 ? -v : v;
                    if (v > peak[k]) {
                        peak[k] = v;
                    }
                }
            }
        }
        for (k = 0; k < f->nsamples; k++) {
            // 16-bit samples as FluidSynth's 24-bit ones
            float norm = (float)(peak[k] ? peak[k] : 1) * 256.0f / 8388607.0f;

            f->samples[k].noise = NOISE_FLOOR / norm;
        }
        free(peak);
    }
    ok = 1;
    goto done;
nomem:
    snprintf(err, (size_t)errlen, "is too big to load (out of memory)");
done:
    for (k = 0; k < 9; k++) {
        free(chunk[k]);
    }
    free(buf);
    fclose(fp);
    if (!ok) {
        mh_sfont_free(f);
        return NULL;
    }
    return f;
}

static int find_preset(const mh_sfont* f, int bank, int prog) {
    int k;

    for (k = 0; f != NULL && k < f->npresets; k++) {
        if (f->presets[k].bank == bank && f->presets[k].prog == prog) {
            return k;
        }
    }
    return -1;
}

//////////////////////////////////////////////////////////////////
// Conversions (FluidSynth's fluid_conv.c)

static float tc2sec(float tc) {
    return powf(2.0f, tc / 1200.0f);
}

static float tc2sec_delay(float tc) {
    if (tc <= -32768.0f) {
        return 0;
    }
    return tc2sec(clampf(tc, -12000.0f, 5000.0f));
}

static float tc2sec_attack(float tc) {
    if (tc <= -32768.0f) {
        return 0;
    }
    return tc2sec(clampf(tc, -12000.0f, 8000.0f));
}

static float cb2amp(float cb) {
    return powf(10.0f, cb / -200.0f);
}

static float concave(float val) {
    int i = (int)val;
    float a, b;

    if (val < 0) {
        return 0;
    }
    if (i >= 127) {
        return 1;
    }
    a = i == 0 ? 0 : -(400.0f / PEAK_ATT) * log10f((127.0f - i) / 127.0f);
    b = i + 1 == 127 ? 1 : -(400.0f / PEAK_ATT) * log10f((127.0f - (i + 1)) / 127.0f);
    return a + (b - a) * (val - i);
}

static float convex(float val) {
    int i = (int)val;
    float a, b;

    if (val < 0) {
        return 0;
    }
    if (i >= 127) {
        return 1;
    }
    a = i == 0 ? 0 : 1.0f + (400.0f / PEAK_ATT) * log10f(i / 127.0f);
    b = i + 1 == 127 ? 1 : 1.0f + (400.0f / PEAK_ATT) * log10f((i + 1) / 127.0f);
    return a + (b - a) * (val - i);
}

static float pan(float c, int left) {
    if (left) {
        c = -c;
    }
    if (c <= -500.0f) {
        return 0;
    }
    if (c >= 500.0f) {
        return 1;
    }
    return sinf((float)((int)c + 500) * (float)(3.14159265358979323846 / 2.0 / 1001.0));
}

static float balance(float b, int left) {
    if (b == 0 || (left && b < 0) || (!left && b > 0)) {
        return 1;
    }
    return cb2amp(b < 0 ? -b : b);
}

//////////////////////////////////////////////////////////////////
// Modulators (FluidSynth's fluid_mod.c)

static const mod_t default_mods[] = {
    { 0x0502, 0, G_ATTEN, 0, 960 },          // velocity to attenuation
    { 0x0102, 0x0c02, G_FC, 0, -2400 },      // velocity to cutoff (always 0, see mod_value())
    { 0x000d, 0, G_VLFO_PITCH, 0, 50 },      // channel pressure to vibrato
    { 0x0081, 0, G_VLFO_PITCH, 0, 50 },      // mod wheel to vibrato
    { 0x0587, 0, G_ATTEN, 0, 960 },          // CC 7 to attenuation
    { 0x028a, 0, G_PAN, 0, 500 },            // CC 10 to pan
    { 0x058b, 0, G_ATTEN, 0, 960 },          // CC 11 to attenuation
    { 0x00db, 0, G_REVERB, 0, 200 },         // CC 91 to reverb
    { 0x00dd, 0, G_CHORUS, 0, 200 },         // CC 93 to chorus
    { 0x020e, 0x0010, G_FINE, 0, 12700 },    // pitch wheel, times its sensitivity
    { 0x0688, 0, G_BALANCE, 0, 960 },        // CC 8 to balance
};

static float source_value(const chan_t* ch, const voice_t* v, uint16_t src, float* range) {
    int i = src & 127;

    if (src & SRC_CC) {
        return ch->cc[i];
    }
    switch (i) {
    case S_NONE:
        return *range;
    case S_VEL:
        return v->gen[G_VELOCITY] >= 0 ? v->gen[G_VELOCITY] : (float)v->vel;
    case S_KEY:
        return v->gen[G_KEYNUM] >= 0 ? v->gen[G_KEYNUM] : (float)v->key;
    case S_POLY:
        return ch->key_pressure[v->key & 127];
    case S_CHPRESS:
        return (float)ch->channel_pressure;
    case S_BEND:
        *range = 0x4000;
        return (float)ch->pitch_bend;
    case S_BENDSENS:
        return ch->bend_range;
    default:
        return 0;
    }
}

static float transform(const mod_t* m, const chan_t* ch, float val, float range, int is_src1) {
    uint16_t src = is_src1 ? m->src1 : m->src2;
    const float norm = val / range, inv = 1.0f - 1.0f / range - norm, top = (range - 1) / range;
    int type = SRC_TYPE(src);

    if (!(src & SRC_CC) && (src & 127) == S_NONE) {
        return 1.0f;
    }
    if (!(src & SRC_BIPOLAR)) {
        val = (src & SRC_NEG) ? inv : norm;
        if (type == 3) {
            val = val >= 0.5f ? 1.0f : 0.0f;
        } else if (type == 1) {
            val = fminf(concave(128 * val), top);
        } else if (type == 2) {
            val = fminf(convex(128 * val), top);
        }
    } else {
        float u = (src & SRC_NEG) ? inv : norm;

        val = u == top ? u : -1.0f + 2.0f * u;
        if (type == 3) {
            val = val >= 0 ? 1.0f : -1.0f;
        } else if (type == 1) {
            val = val >= 0 ? fminf(concave(128 * val), top) : -concave(128 * -val);
        } else if (type == 2) {
            val = val >= 0 ? fminf(convex(128 * val), top) : -convex(128 * -val);
        }
    }
    // the mod wheel's vibrato times the modulation depth range (RPN 5)
    if (is_src1 && (src & SRC_CC) && (src & 127) == 1 && (m->dest == G_VLFO_PITCH || m->dest == G_MLFO_PITCH)) {
        val *= ch->mod_depth / 50.0f;
    }
    return val;
}

static double mod_value(const mod_t* m, const chan_t* ch, const voice_t* v) {
    float r1 = 128, r2 = 128, v1, v2;
    double out;

    // FluidSynth 2.6.1 makes the default velocity-to-cutoff modulator nothing
    if (mod_same(m, &default_mods[1])) {
        return 0;
    }
    v1 = source_value(ch, v, m->src1, &r1); // sets the range of the pitch wheel
    v1 = transform(m, ch, v1, r1, 1);
    v2 = source_value(ch, v, m->src2, &r2);
    v2 = transform(m, ch, v2, r2, 0);
    out = (double)m->amount * v1 * v2;
    return m->trans == 2 ? fabs(out) : out;
}

static int mod_has_source(const mod_t* m, int cc, int src) {
    if (cc) {
        return ((m->src1 & SRC_CC) && (m->src1 & 127) == src) || ((m->src2 & SRC_CC) && (m->src2 & 127) == src);
    }
    return (!(m->src1 & SRC_CC) && (m->src1 & 127) == src) || (!(m->src2 & SRC_CC) && (m->src2 & 127) == src);
}

// FluidSynth's fluid_voice_add_mod_local(): 0 default, 1 overwrite, 2 add
static void add_mod(voice_t* v, const mod_t* m, int mode, int limit) {
    int k;

    if (mode != 0) {
        for (k = 0; k < limit && k < v->nmods; k++) {
            if (mod_same(&v->mods[k], m)) {
                if (mode == 1) {
                    v->mods[k].amount = m->amount;
                } else {
                    v->mods[k].amount += m->amount;
                }
                return;
            }
        }
    }
    if (v->nmods < MAX_MODS) {
        v->mods[v->nmods++] = *m;
    }
}

// FluidSynth's fluid_defpreset_noteon_add_mod_to_voice()
static void add_zone_mods(voice_t* v, const mh_sfont* f, const zone_t* global, const zone_t* local, int mode) {
    const mod_t* list[2 * MAX_MODS];
    int n = 0, k, j, nlocal;

    for (k = 0; k < local->nmods; k++) {
        list[n++] = &f->mods[local->mod0 + k];
    }
    nlocal = n;
    for (k = 0; global != NULL && k < global->nmods && n < MAX_MODS; k++) {
        const mod_t* g = &f->mods[global->mod0 + k];

        for (j = 0; j < nlocal && !mod_same(g, list[j]); j++) {
        }
        if (j == nlocal) {
            list[n++] = g;
        }
    }
    j = v->nmods;
    for (k = 0; k < n; k++) {
        if (mode == 1 || list[k]->amount != 0) {
            add_mod(v, list[k], mode, j);
        }
    }
}

//////////////////////////////////////////////////////////////////
// A voice's parameters (FluidSynth's fluid_voice_update_param())

static double gv(const mh_midi* m, const voice_t* v, int g) {
    double x = v->gen[g] + v->mod[g];

    if (g == G_FINE) {
        x += m->ch[v->chan].fine;
    } else if (g == G_COARSE) {
        x += m->ch[v->chan].coarse;
    }
    return x;
}

static uint32_t ceil_div(uint32_t a, uint32_t b) {
    return (a + b - 1) / b;
}

// the volume envelope's decrement for a section of `count` blocks, Q1.39,
// as FluidSynth's 1/count in double (rounded to the nearest: Q1.31 rounded up
// ended a long release a block early)
static uint64_t q39_step(uint32_t count) {
    return ((1ull << 40) / count + 1) >> 1;
}

static uint32_t blocks_of(float seconds) {
    return (uint32_t)(MH_MIDI_RATE * seconds / MH_MIDI_BLOCK);
}

// calculate_hold_decay_buffers()
static uint32_t hold_decay(const mh_midi* m, const voice_t* v, int base, int key2base, int is_decay) {
    float key = v->gen[G_KEYNUM] >= 0 ? v->gen[G_KEYNUM] : (float)v->key;
    float tc = gv(m, v, base) + gv(m, v, key2base) * (60.0f - key);

    if (is_decay) {
        if (tc > 8000.0f) {
            tc = 8000.0f;
        }
    } else {
        if (tc > 5000.0f) {
            tc = 5000.0f;
        }
        if (tc <= -32768.0f) {
            return 0;
        }
    }
    if (tc < -12000.0f) {
        tc = -12000.0f;
    }
    return (uint32_t)(MH_MIDI_RATE * tc2sec(tc) / MH_MIDI_BLOCK + 0.5f);
}

static uint32_t q16(float x) {
    long v = lroundf(x * 65536.0f);

    return v < 0 ? 0 : v > 65535 ? 65535 : (uint32_t)v;
}

// an envelope's level, Q1.31, held at 0..1 or (max) at 0..2
static uint32_t q31(double x, int max) {
    double v = floor(x * 2147483648.0 + 0.5);
    double hi = max ? 4294967295.0 : 2147483648.0;

    return v < 0 ? 0 : v > hi ? (uint32_t)hi : (uint32_t)v;
}

// a depth in cents or cB, Q.8, held at -lim..lim
static int32_t q8(float x, float lim) {
    return (int32_t)lroundf(clampf(x, -lim, lim) * 256.0f);
}

static void envelope(const mh_midi* m, const voice_t* v, int which, mh_midi_params* p) {
    const int b = which == 0 ? G_VENV_DELAY : G_MENV_DELAY;
    const int k2 = which == 0 ? G_KEY2VENV_HOLD : G_KEY2MENV_HOLD;
    uint32_t count;
    float x;

    p->env[which].delay = blocks_of(tc2sec_delay(clampf(gv(m, v, b), -12000.0f, 5000.0f)));
    count = 1 + blocks_of(tc2sec_attack(clampf(gv(m, v, b + 1), -12000.0f, 8000.0f)));
    p->env[which].attack = ceil_div(1u << 31, count);
    p->env[which].hold = hold_decay(m, v, b + 2, k2, 0);
    count = hold_decay(m, v, b + 3, k2 + 1, 1);
    if (count == 0) {
        p->env[which].decay = which == 0 ? (1ull << 40) - 1 : 0xffffffffull << 8;
    } else {
        p->env[which].decay = which == 0 ? q39_step(count) : (uint64_t)ceil_div(1u << 31, count) << 8;
    }
    p->env[which].sustain = q31(clampf(1.0f - 0.001f * gv(m, v, b + 4), 0, 1), 0);
    // the volume envelope's release is 16 ms at least (FLUID_MIN_VOLENVRELEASE)
    x = clampf(gv(m, v, b + 5), which == 0 ? -7200.0f : -12000.0f, 8000.0f);
    count = 1 + blocks_of(tc2sec_attack(x));
    p->env[which].release = which == 0 ? q39_step(count) : (uint64_t)ceil_div(1u << 31, count) << 8;
}

static void lfo(const mh_midi* m, const voice_t* v, int which, mh_midi_params* p) {
    const int d = which == 0 ? G_MLFO_DELAY : G_VLFO_DELAY;
    uint32_t delay = (uint32_t)(MH_MIDI_RATE * tc2sec_delay(clampf(gv(m, v, d), -12000.0f, 5000.0f)));
    float x = clampf(gv(m, v, d + 1), -16000.0f, 4500.0f);

    // FluidSynth steps an LFO in the first block whose start is past its delay
    p->lfo[which].delay = ceil_div(delay, MH_MIDI_BLOCK);
    // FluidSynth's fluid_ct2hz_real(): whole cents, in double, as the LFO
    // (the steps of a long note's vibrato add up)
    p->lfo[which].incr = (uint32_t)llround(4.0 * MH_MIDI_BLOCK * 440.0 * pow(2.0, ((int)x - 6900) / 1200.0)
        / MH_MIDI_RATE * 2147483648.0);
}

// FluidSynth's fluid_voice_get_lower_boundary_for_attenuation()
static float min_attenuation(const mh_midi* m, const voice_t* v) {
    const chan_t* ch = &m->ch[v->chan];
    float reduction = 0, att = clampf(gv(m, v, G_ATTEN), 0, 1440);
    int k;

    for (k = 0; k < v->nmods; k++) {
        const mod_t* md = &v->mods[k];
        int s1 = md->src1 & 127, s2 = md->src2 & 127;

        if (md->dest == G_ATTEN
            && ((md->src1 & SRC_CC) || (md->src2 & SRC_CC) || s1 == S_CHPRESS || s1 == S_POLY || s1 == S_BEND
                || s2 == S_CHPRESS || s2 == S_POLY || s2 == S_BEND)) {
            float cur = mod_value(md, ch, v), lo = fabsf(md->amount);

            lo = ((md->src1 & SRC_BIPOLAR) || (md->src2 & SRC_BIPOLAR) || md->amount < 0) ? -lo : 0;
            if (cur > lo) {
                reduction += cur - lo;
            }
        }
    }
    att -= reduction;
    return att < 0 ? 0 : att;
}

static void params(const mh_midi* m, const voice_t* v, mh_midi_params* p) {
    const sample_t* s = v->s;
    int64_t start, end, ls, le;
    float root, att, x, noise;
    double pitch;
    int mode;

    memset(p, 0, sizeof(*p));

    // the sample (fluid_rvoice_check_sample_sanity())
    start = (int64_t)s->start + (int)gv(m, v, G_START) + 32768 * (int)gv(m, v, G_STARTC);
    end = (int64_t)s->end + (int)gv(m, v, G_END) + 32768 * (int)gv(m, v, G_ENDC);
    ls = (int64_t)s->loop_start + (int)gv(m, v, G_LSTART) + 32768 * (int)gv(m, v, G_LSTARTC);
    le = (int64_t)s->loop_end + (int)gv(m, v, G_LEND) + 32768 * (int)gv(m, v, G_LENDC);
    start = start < s->start ? s->start : start > s->end ? s->end : start;
    end = end < s->start ? s->start : end > s->end ? s->end : end;
    if (start > end) {
        int64_t t = start;

        start = end;
        end = t;
    }
    mode = (int)v->gen[G_MODE] & 3;
    if (mode == 1 || mode == 3) {
        int64_t lo = s->start, hi = (int64_t)s->end + 1;

        ls = ls < lo ? lo : ls > hi ? hi : ls;
        le = le < lo ? lo : le > hi ? hi : le;
        if (ls > le) {
            int64_t t = ls;

            ls = le;
            le = t;
        }
        if (le < ls + 2 || hi - lo < 2) {
            mode = 0;
        }
    } else {
        mode = 0;
    }
    // a start past the loop's end: in it at once, as having looped (the
    // engine's first frame would wrap it there)
    if (mode != 0 && start >= le) {
        start = ls + (start - ls) % (le - ls);
        p->looped = 1;
    }
    p->start = (uint32_t)start;
    p->end = (uint32_t)end + 1;
    p->loop_start = (uint32_t)ls;
    p->loop_end = (uint32_t)le;
    p->loop_mode = (uint32_t)mode;

    // the pitch: the key's (G_PITCH) and the tunings, against the sample's
    // root; the engine plays the whole cents of it and the modulation, as
    // FluidSynth's fluid_ct2hz_real() does. In double and Q.15 rounded down,
    // so that its whole cents are FluidSynth's (in float and Q.8 a bent note
    // played a cent lower now and then, the phase behind from there on)
    root = v->gen[G_ROOT] > -1 ? v->gen[G_ROOT] * 100.0f : (float)s->root * 100.0f;
    pitch = gv(m, v, G_PITCH) + 100.0 * gv(m, v, G_COARSE) + gv(m, v, G_FINE);
    pitch = pitch < -60000 ? -60000 : pitch > 60000 ? 60000 : pitch;
    p->pitch = (int32_t)floor(pitch * 32768.0);
    p->root = (int32_t)(root - (float)s->adj);
    // in double: the position goes on for minutes in a long note
    p->rate = (uint64_t)llround(fmin((double)s->rate / MH_MIDI_RATE * 4294967296.0, 68719476735.0));

    att = clampf(gv(m, v, G_ATTEN), 0, 1440);
    p->atten = (uint32_t)floorf(att * 16.0f); // the engine plays its whole cB
    p->gain_l = q16(pan(gv(m, v, G_PAN), 1) * balance(gv(m, v, G_BALANCE), 1));
    p->gain_r = q16(pan(gv(m, v, G_PAN), 0) * balance(gv(m, v, G_BALANCE), 0));
    p->gain_rev = q16(clampf(gv(m, v, G_REVERB) / 1000.0f, 0, 1));
    p->gain_cho = q16(clampf(gv(m, v, G_CHORUS) / 1000.0f, 0, 1));
    p->fc = (uint32_t)q8(clampf(gv(m, v, G_FC), 1500.0f, 13500.0f), 13500);
    p->q = (uint32_t)lroundf(clampf(gv(m, v, G_Q), 0, 960.0f));
    p->modlfo_pitch = q8(gv(m, v, G_MLFO_PITCH), 12000);
    p->viblfo_pitch = q8(gv(m, v, G_VLFO_PITCH), 12000);
    p->modenv_pitch = q8(gv(m, v, G_MENV_PITCH), 12000);
    p->modlfo_fc = q8(gv(m, v, G_MLFO_FC), 12000);
    p->modenv_fc = q8(gv(m, v, G_MENV_FC), 12000);
    p->modlfo_vol = q8(gv(m, v, G_MLFO_VOL), 960);
    envelope(m, v, 0, p);
    envelope(m, v, 1, p);
    lfo(m, v, 0, p);
    lfo(m, v, 1, p);

    // inaudible below: FluidSynth's amp_max < amplitude_that_reaches_noise_floor,
    // in a loop of the sample's own after the voice has looped
    x = cb2amp(min_attenuation(m, v));
    noise = NOISE_FLOOR;
    p->end_level[0] = q31(noise / m->gain / x, 1);
    if (mode == 1 && ls >= s->loop_start && le <= s->loop_end) {
        noise = s->noise;
    }
    p->end_level[1] = q31(noise / m->gain / x, 1);
}

//////////////////////////////////////////////////////////////////
// Commands

static void flush(mh_midi* m) {
    uint64_t w[MH_MIDI_CMD_WORDS];
    int k;

    for (k = 0; k < m->nq; k++) {
        const pending_t* c = &m->queue[k];

        mh_midi_pack(w, c->frame, c->voice, c->cmd,
            c->cmd == MH_MIDI_START || c->cmd == MH_MIDI_UPDATE ? &c->p : NULL);
        m->emit(m->ctx, w);
    }
    m->nq = 0;
}

static void copy_u(mh_midi_params* to, const mh_midi_params* from) {
    to->pitch = from->pitch;
    to->atten = from->atten;
    to->gain_l = from->gain_l;
    to->gain_r = from->gain_r;
    to->gain_rev = from->gain_rev;
    to->gain_cho = from->gain_cho;
    to->fc = from->fc;
    to->q = from->q;
    to->modlfo_pitch = from->modlfo_pitch;
    to->viblfo_pitch = from->viblfo_pitch;
    to->modenv_pitch = from->modenv_pitch;
    to->modlfo_fc = from->modlfo_fc;
    to->modenv_fc = from->modenv_fc;
    to->modlfo_vol = from->modlfo_vol;
}

// Commands wait in the queue while they are of one block of frames: an
// UPDATE there replaces the voice's earlier one (or goes into its START).
static void command(mh_midi* m, int voice, int cmd, const mh_midi_params* p) {
    pending_t* c;
    int k;

    if (m->nq > 0 && ((m->now ^ m->queue[0].frame) >> 6) != 0) {
        flush(m);
    }
    if (cmd == MH_MIDI_UPDATE) {
        for (k = m->nq - 1; k >= 0; k--) {
            c = &m->queue[k];
            if (c->voice != voice) {
                continue;
            }
            if (c->cmd == MH_MIDI_START) {
                copy_u(&c->p, p);
                return;
            }
            if (c->cmd == MH_MIDI_UPDATE) {
                memmove(c, c + 1, (size_t)(m->nq - k - 1) * sizeof(*c));
                m->nq--;
            }
            break;
        }
    }
    if (m->nq == QUEUE_MAX) {
        flush(m);
    }
    c = &m->queue[m->nq++];
    c->frame = m->now;
    c->voice = voice;
    c->cmd = cmd;
    if (p != NULL) {
        c->p = *p;
    }
}

static int playing(const voice_t* v) {
    return v->busy;
}

// The engine has ended a released voice by `free_after` at the latest: its
// release starts at a control update after 7 of them (10 ms), and is over
// in its number of blocks.
static void released_at(mh_midi* m, voice_t* v, uint32_t blocks) {
    uint32_t from = m->now;
    uint32_t min = v->start_frame + 7 * MH_MIDI_BLOCK;

    if ((int32_t)(min - from) > 0) {
        from = min;
    }
    v->free_after = from + MH_MIDI_BLOCK * (2 + blocks);
    v->ends_known = 1;
}

static void release(mh_midi* m, int k) {
    voice_t* v = &m->v[k];

    if (v->released) {
        return;
    }
    v->released = 1;
    released_at(m, v, (uint32_t)(((1ull << 39) + v->sent.env[0].release - 1) / v->sent.env[0].release) + 1);
    command(m, k, MH_MIDI_RELEASE, NULL);
}

static void stop(mh_midi* m, int k) {
    voice_t* v = &m->v[k];

    if (v->busy) {
        v->busy = 0;
        command(m, k, MH_MIDI_STOP, NULL);
    }
}

// FluidSynth's fluid_voice_noteoff()
static void noteoff_voice(mh_midi* m, int k) {
    voice_t* v = &m->v[k];
    const chan_t* ch = &m->ch[v->chan];

    if (ch->cc[66] >= 64 && ch->sostenuto_id > v->id) {
        v->state = V_SOSTENUTO;
    } else if (ch->cc[64] >= 64) {
        v->state = V_SUSTAINED;
    } else {
        release(m, k);
    }
}

// The modulators of the voices of a channel with this source again, and an
// UPDATE for each voice whose parameters change (fluid_voice_modulate()).
static void modulate(mh_midi* m, int chan, int cc, int src, int key) {
    const chan_t* ch = &m->ch[chan];
    mh_midi_params p, q;
    int k, j, n;

    for (k = 0; k < MH_MIDI_VOICES; k++) {
        voice_t* v = &m->v[k];

        if (!playing(v) || v->chan != chan || (key >= 0 && v->key != key)) {
            continue;
        }
        for (j = 0; j < v->nmods && (src >= 0 && !mod_has_source(&v->mods[j], cc, src)); j++) {
        }
        if (j == v->nmods) {
            continue;
        }
        for (n = 0; n < NGEN; n++) {
            v->mod[n] = 0;
        }
        for (j = 0; j < v->nmods; j++) {
            v->mod[v->mods[j].dest] += mod_value(&v->mods[j], ch, v);
        }
        params(m, v, &p);
        q = v->sent;
        copy_u(&q, &p);
        if (memcmp(&q, &v->sent, sizeof(q)) != 0) {
            v->sent = q;
            // the whole block as the voice has it: the engine takes all of it
            command(m, k, MH_MIDI_UPDATE, &q);
        }
    }
}

// FluidSynth's fluid_voice_get_overflow_prio() with its default scores
static float priority(const mh_midi* m, const voice_t* v) {
    float prio = 0;
    uint32_t age = m->now - v->start_frame;

    if (m->ch[v->chan].drum) {
        prio += 4000;
    } else if (v->released) {
        prio -= 2000;
    } else if (v->state != V_ON) {
        prio -= 1000;
    }
    prio += 1000.0f * MH_MIDI_RATE / (float)(age < 1 ? 1 : age);
    prio += 500.0f / fmaxf(clampf(gv(m, v, G_ATTEN), 0, 1440), 0.1f);
    return prio;
}

static int alloc_voice(mh_midi* m) {
    int k, best = -1;
    float best_prio = 1e30f;

    for (k = 0; k < MH_MIDI_VOICES; k++) {
        voice_t* v = &m->v[k];

        if (v->busy && v->ends_known && (int32_t)(m->now - v->free_after) >= 0) {
            v->busy = 0;
        }
        if (!v->busy) {
            return k;
        }
    }
    for (k = 0; k < MH_MIDI_VOICES; k++) {
        const voice_t* v = &m->v[k];
        float prio;

        // OVERFLOW_PRIO_CANNOT_KILL
        if (v->stolen && v->stolen_at / MH_MIDI_BLOCK == m->now / MH_MIDI_BLOCK) {
            continue;
        }
        prio = priority(m, v);
        if (prio < best_prio) {
            best_prio = prio;
            best = k;
        }
    }
    if (best >= 0) {
        m->v[best].stolen = 1;
        m->v[best].stolen_at = m->now;
    }
    return best;
}

// FluidSynth's fluid_defpreset_noteon()
static void noteon(mh_midi* m, int chan, int key, int vel) {
    chan_t* ch = &m->ch[chan];
    const mh_sfont* f = m->font;
    const preset_t* pr;
    const zone_t *pg, *ig;
    uint32_t id;
    int pz, iz, k, j, g;

    if (f == NULL || ch->preset < 0) {
        return;
    }
    // other voices of the key are released (synth.note-cut 0)
    id = ++m->noteid;
    for (k = 0; k < MH_MIDI_VOICES; k++) {
        voice_t* v = &m->v[k];

        if (playing(v) && v->chan == chan && v->key == key && v->id != id) {
            noteoff_voice(m, k);
        }
    }
    // the zones from the last to the first, as FluidSynth's loader puts
    // them in its lists (fluid_defpreset_add_zone(), fluid_inst_add_zone()):
    // the voices take the same slots
    pr = &f->presets[ch->preset];
    pg = pr->z.global >= 0 ? &f->zones[pr->z.global] : NULL;
    for (pz = pr->z.nzones - 1; pz >= 0; pz--) {
        const zone_t* pzone = &f->zones[pr->z.zone0 + pz];
        const zlist_t* inst;

        if (key < pzone->keylo || key > pzone->keyhi || vel < pzone->vello || vel > pzone->velhi
            || pzone->link < 0 || pzone->link >= f->ninsts) {
            continue;
        }
        inst = &f->insts[pzone->link];
        ig = inst->global >= 0 ? &f->zones[inst->global] : NULL;
        for (iz = inst->nzones - 1; iz >= 0; iz--) {
            const zone_t* izone = &f->zones[inst->zone0 + iz];
            voice_t* v;
            mh_midi_params p;
            int slot;

            if (key < izone->keylo || key > izone->keyhi || vel < izone->vello || vel > izone->velhi
                || izone->link < 0 || izone->link >= f->nsamples || !f->samples[izone->link].ok) {
                continue;
            }
            slot = alloc_voice(m);
            if (slot < 0) {
                return;
            }
            v = &m->v[slot];
            {
                int stolen = v->stolen;
                uint32_t stolen_at = v->stolen_at;

                memset(v, 0, sizeof(*v));
                v->stolen = stolen;
                v->stolen_at = stolen_at;
            }
            v->chan = chan;
            v->key = key;
            v->vel = vel;
            v->id = id;
            v->start_frame = m->now;
            v->s = &f->samples[izone->link];
            for (g = 0; g < NGEN; g++) {
                v->gen[g] = gen_default[g];
            }
            // instrument generators: the zone's, else the global zone's
            for (g = 0; g < G_PITCH; g++) {
                if (izone->set & (1ull << g)) {
                    v->gen[g] = izone->gen[g];
                } else if (ig != NULL && (ig->set & (1ull << g))) {
                    v->gen[g] = ig->gen[g];
                }
            }
            for (j = 0; j < (int)(sizeof(default_mods) / sizeof(default_mods[0])); j++) {
                add_mod(v, &default_mods[j], 0, 0);
            }
            add_zone_mods(v, f, ig, izone, 1);
            // preset generators are added
            for (g = 0; g < G_PITCH; g++) {
                if (pzone->set & (1ull << g)) {
                    v->gen[g] += pzone->gen[g];
                } else if (pg != NULL && (pg->set & (1ull << g))) {
                    v->gen[g] += pg->gen[g];
                }
            }
            add_zone_mods(v, f, pg, pzone, 2);
            // the key's pitch (fluid_voice_calculate_pitch())
            {
                float root = v->gen[G_ROOT] > -1 ? v->gen[G_ROOT] * 100.0f : (float)v->s->root * 100.0f;
                float k2 = v->gen[G_KEYNUM] >= 0 ? v->gen[G_KEYNUM] : (float)key;

                v->gen[G_PITCH] = v->gen[G_SCALE] * (k2 - root / 100.0f) + root;
            }
            for (j = 0; j < v->nmods; j++) {
                v->mod[v->mods[j].dest] += mod_value(&v->mods[j], ch, v);
            }
            v->excl = (int)v->gen[G_EXCL];
            // the exclusive class ends the channel's other voices of the class
            // (fluid_synth_kill_by_exclusive_class_LOCAL())
            if (v->excl != 0) {
                for (j = 0; j < MH_MIDI_VOICES; j++) {
                    voice_t* o = &m->v[j];

                    if (j != slot && playing(o) && o->chan == chan && o->excl == v->excl && o->id != id) {
                        o->excl = 0;
                        o->released = 1;
                        released_at(m, o, KILL_BLOCKS);
                        command(m, j, MH_MIDI_KILL, NULL);
                    }
                }
            }
            params(m, v, &p);
            v->sent = p;
            v->busy = 1;
            v->state = V_ON;
            command(m, slot, MH_MIDI_START, &p);
        }
    }
}

static void noteoff(mh_midi* m, int chan, int key) {
    int k;

    for (k = 0; k < MH_MIDI_VOICES; k++) {
        voice_t* v = &m->v[k];

        if (playing(v) && v->chan == chan && v->key == key && v->state == V_ON && !v->released) {
            noteoff_voice(m, k);
        }
    }
}

static void program(mh_midi* m, int chan, int prog) {
    chan_t* ch = &m->ch[chan];
    int p = find_preset(m->font, ch->bank, prog);

    // FluidSynth's fluid_synth_program_change(): the bank's program, else
    // bank 0's (128's on a drum channel), else program 0 of it
    if (p < 0) {
        int bank = ch->drum ? 128 : 0;

        p = find_preset(m->font, bank, prog);
        if (p < 0) {
            p = find_preset(m->font, bank, 0);
        }
    }
    ch->prog = prog;
    ch->preset = p;
}

static void init_ctrl(chan_t* ch, int all_ctrl_off) {
    int i;

    ch->channel_pressure = 0;
    ch->pitch_bend = 0x2000;
    ch->fine = ch->coarse = 0;
    if (all_ctrl_off) {
        for (i = 0; i < 120; i++) {
            if ((i >= 91 && i <= 95) || (i >= 70 && i <= 79) || i == 0 || i == 32 || i == 7 || i == 39 || i == 10
                || i == 42 || i == 8 || i == 40) {
                continue;
            }
            ch->cc[i] = 0;
        }
    } else {
        memset(ch->cc, 0, sizeof(ch->cc));
    }
    memset(ch->key_pressure, 0, sizeof(ch->key_pressure));
    ch->cc[100] = ch->cc[101] = 127;
    ch->cc[98] = ch->cc[99] = 127;
    ch->cc[11] = ch->cc[43] = 127;
    if (!all_ctrl_off) {
        ch->bend_range = 2;
        ch->mod_depth = 50;
        for (i = 70; i <= 79; i++) {
            ch->cc[i] = 64;
        }
        ch->cc[7] = 100;
        ch->cc[10] = 64;
        ch->cc[8] = 64;
    }
}

static void reset_channel(mh_midi* m, int chan) {
    chan_t* ch = &m->ch[chan];

    memset(ch, 0, sizeof(*ch));
    ch->drum = chan == 9;
    ch->bank = ch->drum ? 128 : 0;
    init_ctrl(ch, 0);
    program(m, chan, 0);
}

static void system_reset(mh_midi* m) {
    int k;

    for (k = 0; k < MH_MIDI_VOICES; k++) {
        stop(m, k);
    }
    for (k = 0; k < 16; k++) {
        reset_channel(m, k);
    }
}

static void damp_sustain(mh_midi* m, int chan) {
    int k;

    for (k = 0; k < MH_MIDI_VOICES; k++) {
        if (playing(&m->v[k]) && m->v[k].chan == chan && m->v[k].state == V_SUSTAINED) {
            m->v[k].state = V_ON;
            release(m, k);
        }
    }
}

static void damp_sostenuto(mh_midi* m, int chan) {
    int k;

    for (k = 0; k < MH_MIDI_VOICES; k++) {
        voice_t* v = &m->v[k];

        if (playing(v) && v->chan == chan && v->state == V_SOSTENUTO) {
            if (m->ch[chan].cc[64] >= 64) {
                v->state = V_SUSTAINED;
            } else {
                v->state = V_ON;
                release(m, k);
            }
        }
    }
}

static void data_entry(mh_midi* m, int chan) {
    chan_t* ch = &m->ch[chan];
    int msb = ch->cc[6], lsb = ch->cc[38], data = (msb << 7) + lsb;

    if (ch->nrpn_active || ch->cc[101] != 0) {
        return; // NRPNs (SoundFont, AWE32, GS) are not taken
    }
    switch (ch->cc[100]) {
    case 0:
        ch->bend_range = msb + lsb / 100.0f;
        modulate(m, chan, 0, S_BENDSENS, -1);
        break;
    case 1:
        ch->fine = (data - 8192) * (100.0f / 8192.0f);
        modulate(m, chan, 0, -1, -1);
        break;
    case 2:
        ch->coarse = (float)(msb - 64);
        modulate(m, chan, 0, -1, -1);
        break;
    case 5:
        if (!ch->drum) {
            ch->mod_depth = msb * 100.0f + lsb * 100.0f / 128.0f;
            modulate(m, chan, 1, 1, -1);
        }
        break;
    default:
        break;
    }
}

static void control_change(mh_midi* m, int chan, int num, int value) {
    chan_t* ch = &m->ch[chan];
    int k;

    ch->cc[num] = (uint8_t)value;
    switch (num) {
    case 0: // bank select, FluidSynth's styles
        if (m->style == STYLE_GS) {
            ch->bank = ch->drum ? 128 + value : value;
        } else if (m->style == STYLE_XG) {
            ch->drum = value == 120 || value == 126 || value == 127;
            if (ch->drum) {
                ch->bank = 128;
            }
        } else if (m->style == STYLE_GM2) {
            if (value == 120) {
                ch->drum = 1;
                ch->bank = 128;
            } else if (value == 121) {
                ch->drum = 0;
                ch->bank = 0;
            }
        }
        break;
    case 32:
    case 65:
    case 68:
    case 122:
    case 124:
    case 125:
    case 126:
    case 127:
        break;
    case 64:
        if (value < 64) {
            damp_sustain(m, chan);
        }
        break;
    case 66:
        if (value < 64) {
            damp_sostenuto(m, chan);
        } else {
            ch->sostenuto_id = m->noteid + 1;
        }
        break;
    case 6:
    case 38:
        data_entry(m, chan);
        break;
    case 98:
    case 99:
        ch->cc[6] = ch->cc[38] = 0;
        ch->nrpn_active = 1;
        break;
    case 100:
    case 101:
        ch->cc[6] = ch->cc[38] = 0;
        ch->nrpn_active = 0;
        break;
    case 120: // all sound off
        for (k = 0; k < MH_MIDI_VOICES; k++) {
            if (playing(&m->v[k]) && m->v[k].chan == chan) {
                stop(m, k);
            }
        }
        break;
    case 121: // reset all controllers
        init_ctrl(ch, 1);
        damp_sustain(m, chan);
        damp_sostenuto(m, chan);
        modulate(m, chan, 0, -1, -1);
        break;
    case 123: // all notes off
        for (k = 0; k < MH_MIDI_VOICES; k++) {
            if (playing(&m->v[k]) && m->v[k].chan == chan) {
                noteoff_voice(m, k);
            }
        }
        break;
    default:
        modulate(m, chan, 1, num, -1);
        break;
    }
}

//////////////////////////////////////////////////////////////////
// The interface

mh_midi* mh_midi_new(mh_midi_emit emit, void* ctx) {
    mh_midi* m = calloc(1, sizeof(*m));
    int k;

    if (m == NULL) {
        return NULL;
    }
    m->emit = emit;
    m->ctx = ctx;
    m->gain = 0.5f;
    m->style = STYLE_GS;
    for (k = 0; k < 16; k++) {
        reset_channel(m, k);
    }
    return m;
}

void mh_midi_free(mh_midi* m) {
    free(m);
}

void mh_midi_set_font(mh_midi* m, mh_sfont* font) {
    int k;

    m->font = font;
    for (k = 0; k < MH_MIDI_VOICES; k++) {
        m->v[k].busy = 0;
    }
    for (k = 0; k < 16; k++) {
        program(m, k, m->ch[k].prog);
    }
}

void mh_midi_set_gain(mh_midi* m, float gain) {
    m->gain = gain > 0.0001f ? gain : 0.0001f;
}

static void at(mh_midi* m, uint32_t frame) {
    if ((int32_t)(frame - m->now) > 0 || !m->started) {
        m->now = frame;
        m->started = 1;
    }
}

void mh_midi_event(mh_midi* m, uint32_t frame, int status, int data1, int data2) {
    int chan = status & 15;
    chan_t* ch = &m->ch[chan];

    at(m, frame);
    data1 &= 127;
    data2 &= 127;
    switch (status & 0xf0) {
    case 0x80:
        noteoff(m, chan, data1);
        break;
    case 0x90:
        if (data2 == 0) {
            noteoff(m, chan, data1);
        } else {
            noteon(m, chan, data1, data2);
        }
        break;
    case 0xa0:
        ch->key_pressure[data1] = (uint8_t)data2;
        modulate(m, chan, 0, S_POLY, data1);
        break;
    case 0xb0:
        control_change(m, chan, data1, data2);
        break;
    case 0xc0:
        program(m, chan, data1);
        break;
    case 0xd0:
        ch->channel_pressure = data1;
        modulate(m, chan, 0, S_CHPRESS, -1);
        break;
    case 0xe0:
        ch->pitch_bend = data1 | data2 << 7;
        modulate(m, chan, 0, S_BEND, -1);
        break;
    default:
        break;
    }
}

void mh_midi_sysex(mh_midi* m, uint32_t frame, const uint8_t* d, int len) {
    at(m, frame);
    if (len < 4) {
        return;
    }
    // GM / GM2 system on
    if (d[0] == 0x7e && d[2] == 0x09 && (d[3] == 0x01 || d[3] == 0x03)) {
        m->style = d[3] == 0x03 ? STYLE_GM2 : STYLE_GM;
        system_reset(m);
        return;
    }
    // Roland GS DT1
    if (d[0] == 0x41 && d[2] == 0x42 && d[3] == 0x12 && len >= 9) {
        int addr = d[4] << 16 | d[5] << 8 | d[6], sum = 0, i;

        for (i = 4; i < len - 1; i++) {
            sum += d[i];
        }
        if (((0x80 - (sum & 0x7f)) & 0x7f) != d[len - 1]) {
            return;
        }
        if (addr == 0x40007f && len == 9 && (d[7] == 0 || d[7] == 0x7f)) {
            m->style = d[7] == 0 ? STYLE_GS : STYLE_GM;
            system_reset(m);
        } else if (m->style == STYLE_GS && (addr & 0xfff0ff) == 0x401015 && len == 9 && d[7] <= 2) {
            // use for rhythm part
            int part = (addr >> 8) & 15;
            int chan = part >= 10 ? part : part == 0 ? 9 : part - 1;

            control_change(m, chan, 121, 0);
            m->ch[chan].drum = d[7] != 0;
            m->ch[chan].bank = d[7] != 0 ? 128 : 0;
            program(m, chan, 0);
        }
        return;
    }
    // Yamaha XG system on
    if (d[0] == 0x43 && d[2] == 0x4c && len >= 7 && d[3] == 0 && d[4] == 0 && (d[5] == 0x7e || d[5] == 0x7f)
        && d[6] == 0) {
        m->style = STYLE_XG;
        system_reset(m);
    }
}

void mh_midi_reset(mh_midi* m, uint32_t frame) {
    at(m, frame);
    system_reset(m);
}

void mh_midi_stop_all(mh_midi* m, uint32_t frame) {
    int k;

    at(m, frame);
    for (k = 0; k < MH_MIDI_VOICES; k++) {
        stop(m, k);
    }
}

void mh_midi_voices(mh_midi* m, uint32_t frame, const uint32_t* bits) {
    int k;

    for (k = 0; k < MH_MIDI_VOICES; k++) {
        voice_t* v = &m->v[k];

        // a voice started a block before the report and not playing in it has ended
        if (v->busy && !(bits[k >> 5] >> (k & 31) & 1) && (int32_t)(frame - (v->start_frame + MH_MIDI_BLOCK)) >= 0) {
            v->busy = 0;
        }
    }
}

void mh_midi_flush(mh_midi* m) {
    flush(m);
}

int mh_midi_busy(const mh_midi* m) {
    int k, n = 0;

    for (k = 0; k < MH_MIDI_VOICES; k++) {
        n += m->v[k].busy;
    }
    return n;
}
