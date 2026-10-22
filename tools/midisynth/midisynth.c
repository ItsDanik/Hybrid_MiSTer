// C model of the core's MIDI voice engine: see midisynth.h. Every number
// format and every rounding here is the definition the RTL follows
// (docs/midi_synth.md, "Design"); the conversions from the SoundFont are
// FluidSynth 2.6.1's and happen on the ARM (hps/mister_midi.c).
#include "midisynth.h"
#include "midisynth_tables.h"
#include "../../hps/mister_midi.h"

#include <stdlib.h>
#include <string.h>

#define ONE (1 << 23)           // 1.0 of the modulation envelope's value, Q1.23
#define LFO_ONE (1ll << 31)     // 1.0 of an LFO, Q1.31
#define ENV_ONE (1ll << 31)     // 1.0 of an envelope's level, Q1.31
#define ENV_X1 (1ll << 39)      // 1.0 of an envelope's x, Q1.39
#define AMP_MAX ((1 << 29) - 1) // a voice's gain (Q3.26) is held under 8.0
#define Y_MAX ((1 << 26) - 1)   // the filter's output is held at 27 bits
#define H_MAX ((1ll << 55) - 1) // its state at 56 (a 96 dB resonance at 20 Hz needs 54)
#define INCR_MAX ((1ull << 40) - 1) // a voice's step, Q8.32, held under 256 samples a frame
#define CMDS_A_FRAME 32         // commands made in one frame at most
#define STARTS_A_FRAME 8        // of which STARTs

enum { SEC_DELAY, SEC_ATTACK, SEC_HOLD, SEC_DECAY, SEC_SUSTAIN, SEC_RELEASE, SEC_FINISHED };

typedef struct {
    int sec;
    uint32_t cnt;
    int64_t x;                  // Q1.39, 41 bits
} env_t;

typedef struct {
    int64_t val;                // Q1.31, 33 bits
    int down;
} lfo_t;

typedef struct {
    int on;
    int due;                    // started in this frame: a control update in it
    int first;                  // no control update yet
    mh_midi_params p;
    uint64_t pos;               // sample number << 32 | fraction (FluidSynth's 32.32)
    uint64_t incr;              // Q8.32
    int looped;
    int release;                // 1 RELEASE, 2 KILL, waiting for a control update
    int killed;                 // the volume envelope's release is the kill rate
    uint32_t blocks;            // control updates made
    env_t env[2];
    lfo_t lfo[2];
    int32_t coef[4];            // b02, b1, a1, a2: Q2.24
    int32_t fres, fres_incr;    // the filter's cutoff, its step a frame: cents Q.16
    int fres_n;                 // frames the cutoff still moves
    int silent;                 // no gain in this block: no filter either
    int64_t h1, h2;             // the filter's state (direct form II), Q.4 as the samples
    int32_t amp, amp_step;      // Q3.26
    int32_t send[4], send_step[4]; // left, right, reverb, chorus: Q0.22
    int32_t send_other[4];      // the sends of FluidSynth's other rvoice of the voice
    int ran;                    // frames since the control update
} voice_t;

// The effects (docs/midi_synth.md, "The effects"): FluidSynth's reverb
// "free" (Freeverb) and its chorus, every delay line in one memory of 24-bit
// words. The reverb works in units of the sums >> REV_SHIFT (its allpasses'
// lines in twice that), the chorus's line in units of the sums >> CHO_SHIFT
// (its input is not scaled down).
// Every product is rounded to the nearest, half up: rounding down left a
// bias the combs' feedback made into an error of -28 dB.
#define REV_SHIFT 6
#define CHO_SHIFT 8
#define FX_MAX ((1 << 23) - 1)
#ifdef FX_PEAKS
static int64_t fx_peak[4];      // stored: comb, allpass, chorus; the allpasses' output
#define FX_PEAK(k, v) do { int64_t a_ = (v) < 0 ? -(v) : (v); if (a_ > fx_peak[k]) fx_peak[k] = a_; } while (0)
#else
#define FX_PEAK(k, v) do { } while (0)
#endif
#define REV_LINES 24            // the combs, left then right, then the allpasses
#define FX_WORDS 27499          // all the delay lines: 25,450 the reverb's, 2,049 the chorus's

static const uint16_t rev_len[REV_LINES] = {
    1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617,
    1139, 1211, 1300, 1379, 1445, 1514, 1580, 1640,
    556, 441, 341, 225,
    579, 464, 364, 248,
};

typedef struct {
    int32_t mem[FX_WORDS];
    uint16_t base[REV_LINES];   // where each reverb line starts in mem
    uint16_t ptr[REV_LINES];
    int32_t store[16];          // the combs' low-pass filters
    // the chorus, its line at FX_WORDS - MS_CHO_SIZE
    int cin;                    // where the next input goes
    int center;                 // the modulation's centre
    int rate;                   // samples since the LFOs' last step
    uint64_t phase[3];          // the LFOs, Q.48 of a turn
    int out[3];                 // each block's read position
    int32_t frac[3];            // its fraction, Q.18
    int64_t last[3];            // its interpolator's last output
} fx_t;

struct midisynth {
    const int16_t* smp;
    uint32_t nsmp;
    voice_t v[MH_MIDI_VOICES];
    uint64_t (*q)[MH_MIDI_CMD_WORDS];
    int qhead, qcount;
    int aligned;
    fx_t fx;
};

#ifdef MS_CONTROL_HOOK
// a test's look at every control update (sim/midi_unit_shim.c)
static void MS_CONTROL_HOOK(const voice_t* before, const voice_t* after);
#endif

static int32_t sample(const midisynth* ms, uint32_t i) {
    return i < ms->nsmp ? ms->smp[i] : 0;
}

static int64_t floor_shift(int64_t v, int n) {
    return v >> n; // arithmetic: rounds down
}

// 10^(-c/200), c in 1/16 cB, Q0.26
// FluidSynth's fluid_cb2amp() of an attenuation in 1/4096 cB: of a positive
// one its whole cB, of a negative one (an LFO making a voice louder) the
// gain above 1, in 1/16 cB. Q3.26.
static int32_t cb2amp(int32_t c) {
    uint32_t e, n;

    if (c >= 0) {
        c >>= 12;
        return c > 1440 ? 0 : (int32_t)ms_cb2amp[c];
    }
    e = (uint32_t)(((uint64_t)(uint32_t)(-(c >> 8)) * MS_K_CB) >> 16);
    n = (e >> 10) + ((e & 1023) != 0);
    if (n >= 3) {
        return AMP_MAX;
    }
    return (int32_t)((uint64_t)ms_amp[(1024 - (e & 1023)) & 1023] << n);
}

// FluidSynth's fluid_convex(127 * x), linear between the table's points
static int32_t convex(int32_t x) {
    int64_t v = (int64_t)x * 127;
    int i = (int)(v >> 23);
    int64_t f = v & (ONE - 1);

    if (x <= 0) {
        return 0;
    }
    if (i >= 127) {
        return ms_convex[127];
    }
    return ms_convex[i] + (int32_t)floor_shift((int64_t)(ms_convex[i + 1] - ms_convex[i]) * f, 23);
}

// The volume envelope from its attack (linear in amplitude) to its release
// (linear in dB) at the same level: 1 + 200 / 960 * log10(x), Q1.31
static int64_t att2rel(int64_t x) {
    int n = 0;
    int64_t l, y;

    while ((x << n) < ENV_ONE) {
        n++;
    }
    // x << n is 1.m, Q1.31; log2(x) = log2(1.m) - n, Q.16
    l = (int64_t)ms_log2[((x << n) >> 21) & 1023] - ((int64_t)n << 16);
    y = ENV_ONE + floor_shift(l * MS_K_REL, 8);
    return y < 0 ? 0 : y > ENV_ONE ? ENV_ONE : y;
}

// as FluidSynth's fluid_adsr_env_calc(), x Q1.39: the steps of a long decay or
// release add up (Q1.31 ended a drum's release a block before FluidSynth's)
static void env_step(env_t* e, const mh_midi_params* p, int which, uint64_t release) {
    const uint32_t delay = p->env[which].delay, hold = p->env[which].hold;
    const int64_t attack = (int64_t)p->env[which].attack << 8, decay = (int64_t)p->env[which].decay;
    const int64_t sustain = (int64_t)p->env[which].sustain << 8;

    for (;;) {
        switch (e->sec) {
        case SEC_DELAY:
            if (e->cnt >= delay) {
                e->sec = SEC_ATTACK;
                e->cnt = 0;
                continue;
            }
            e->x = 0;
            e->cnt++;
            return;
        case SEC_ATTACK:
            e->x += attack;
            if (e->x >= ENV_X1) {
                e->x = ENV_X1;
                e->sec = SEC_HOLD;
                e->cnt = 0;
            } else {
                e->cnt++;
            }
            return;
        case SEC_HOLD:
            if (e->cnt >= hold) {
                e->sec = SEC_DECAY;
                e->cnt = 0;
                continue;
            }
            e->cnt++;
            return;
        case SEC_DECAY:
            e->x -= decay;
            if (e->x < sustain) {
                e->x = sustain;
                e->sec = SEC_SUSTAIN;
                e->cnt = 0;
            } else {
                e->cnt++;
            }
            return;
        case SEC_SUSTAIN:
            return;
        case SEC_RELEASE:
            e->x -= (int64_t)release;
            if (e->x < 0) {
                e->x = 0;
                e->sec = SEC_FINISHED;
            }
            return;
        default:
            e->x = 0;
            return;
        }
    }
}

static void lfo_step(lfo_t* l, uint32_t incr) {
    l->val += l->down ? -(int64_t)incr : (int64_t)incr;
    if (l->val > LFO_ONE) {
        l->down = 1;
        l->val = 2 * LFO_ONE - l->val;
    } else if (l->val < -LFO_ONE) {
        l->down = 0;
        l->val = -2 * LFO_ONE - l->val;
    }
}

// the filter's coefficients for a cutoff (cents Q.16) and Q (cB), FluidSynth's
// fluid_iir_filter_calculate_coefficients()
static void coefs(voice_t* v, int32_t fres, int32_t q) {
    int32_t c = fres >> 16, i = c < MS_FC_MIN ? 0 : c - MS_FC_MIN, s, co, alpha, a0;

    s = ms_sin[i];
    co = ms_cos[i];
    alpha = (int32_t)(((int64_t)s * ms_inv2q[q]) >> 24);
    a0 = (int32_t)((1ll << 48) / ((1 << 24) + alpha));
    v->coef[2] = -(int32_t)floor_shift((int64_t)co * a0, 23);
    v->coef[3] = (int32_t)((((int64_t)(1 << 24) - alpha) * a0) >> 24);
    v->coef[1] = (int32_t)((((((int64_t)(1 << 24) - co) * a0) >> 24) * ms_isq[q]) >> 24);
    v->coef[0] = v->coef[1] >> 1;
}

// a voice's control update, once a block (docs/midi_synth.md, "The control
// engine")
static void control(voice_t* v) {
    const mh_midi_params* p = &v->p;
    int32_t menv, lfovol, c, target, cents, fres, d, q;
    int64_t mlfo, vlfo;
    uint32_t sends[4];
    int64_t m, x;
    unsigned __int128 prod, inc;
    uint32_t b;
    int k, sh;

    // 1. a release waits for 7 updates (FluidSynth's 10 ms of a note at least)
    if (v->release && v->blocks >= 7) {
        if (v->env[0].sec == SEC_ATTACK && v->env[0].x > 0) {
            v->env[0].x = att2rel(v->env[0].x >> 8) << 8;
        }
        if (v->env[1].sec == SEC_ATTACK && v->env[1].x > 0) {
            v->env[1].x = (int64_t)convex((int32_t)(v->env[1].x >> 16)) << 16;
        }
        v->env[0].sec = v->env[1].sec = SEC_RELEASE;
        v->env[0].cnt = v->env[1].cnt = 0;
        if (v->release == 2) {
            v->killed = 1;
        }
        v->release = 0;
    }
    b = v->blocks++;

    // 2. the envelopes
    env_step(&v->env[0], p, 0, v->killed ? MS_KILL_DEC : p->env[0].release);
    if (v->env[0].sec == SEC_FINISHED) {
        v->on = 0;
        return;
    }
    env_step(&v->env[1], p, 1, p->env[1].release);

    // 3. the LFOs, once their delay is over
    for (k = 0; k < 2; k++) {
        if (b >= p->lfo[k].delay) {
            lfo_step(&v->lfo[k], p->lfo[k].incr);
        }
    }
    mlfo = v->lfo[0].val;
    vlfo = v->lfo[1].val;

    // 4. the modulation envelope's value
    menv = (int32_t)(v->env[1].x >> 16);
    menv = v->env[1].sec == SEC_ATTACK ? convex(menv) : menv;

    // 7, 8. the gain, and the end of an inaudible voice: as FluidSynth, the
    // attenuation's gain times that of the envelope and the LFO, in 1/4096 cB
    x = v->env[0].x;
    lfovol = (int32_t)floor_shift(mlfo * p->modlfo_vol, 27);
    if (v->env[0].sec == SEC_DELAY) {
        target = 0;
    } else if (v->env[0].sec == SEC_ATTACK) {
        target = (int32_t)(((int64_t)cb2amp((int32_t)p->atten << 8) * cb2amp(-lfovol)) >> 26);
        target = (int32_t)(((int64_t)target * (x >> 8)) >> 31);
    } else {
        if (x < (int64_t)p->end_level[v->looped] << 8) {
            v->on = 0;
            return;
        }
        c = (int32_t)(((ENV_X1 - x) * 960) >> 27) - lfovol;
        target = (int32_t)(((int64_t)cb2amp((int32_t)p->atten << 8) * cb2amp(c)) >> 26);
    }
    target = target > AMP_MAX ? AMP_MAX : target;
    // the sends go to theirs in the block too, from 0 at the start
    // (FluidSynth's fluid_rvoice_buffers_mix()): exactly, both being
    // multiples of 64
    sends[0] = p->gain_l;
    sends[1] = p->gain_r;
    sends[2] = p->gain_rev;
    sends[3] = p->gain_cho;
    for (k = 0; k < 4; k++) {
        v->send_step[k] = (int32_t)floor_shift(((int64_t)sends[k] << 6) - v->send[k], 6);
    }
    v->ran = 0;
    v->amp_step = (int32_t)floor_shift((int64_t)target - v->amp, 6);

    // 5. the pitch
    // whole cents of it and the modulation, towards 0 (FluidSynth's
    // fluid_ct2hz_real()), against the root
    m = ((int64_t)p->pitch << 24) + mlfo * p->modlfo_pitch + vlfo * p->viblfo_pitch
        + ((int64_t)menv << 8) * p->modenv_pitch;
    cents = (int32_t)(m >= 0 ? m >> 39 : -(-m >> 39)) - p->root;
    q = cents >= 0 ? cents / 1200 : -((-cents + 1199) / 1200);
    // rate Q4.32 times 2^(cents/1200) Q1.32, to Q8.32 rounded down as
    // FluidSynth's fluid_phase_set_float()
    prod = (unsigned __int128)p->rate * ms_pitch[cents - q * 1200];
    sh = 32 - q;
    if (sh >= 128) {
        inc = 0;
    } else if (sh >= 0) {
        inc = prod >> sh;
    } else {
        inc = (prod >> (88 + sh)) != 0 ? INCR_MAX : prod << -sh;
    }
    if (inc > INCR_MAX) {
        inc = INCR_MAX;
    }
    v->incr = inc == 0 ? 1 : (uint64_t)inc;

    // 6. the filter, as FluidSynth's fluid_iir_filter_calc(): the cutoff in
    // cents Q.16; a jump of more than a cent is spread over 64 * clip(q, 1, 5)
    // frames, the coefficients worked out again at each
    m = ((int64_t)p->fc << 31) + mlfo * p->modlfo_fc + ((int64_t)menv << 8) * p->modenv_fc;
    m = floor_shift(m, 23);
    fres = m < MS_FRES_LO ? MS_FRES_LO : m > MS_FRES_HI ? MS_FRES_HI : (int32_t)m;
    q = p->q > 960 ? 960 : (int32_t)p->q;
    d = fres - v->fres;
    if (!v->first && (d > 65536 || d < -65536)) {
        v->fres_incr = (int32_t)floor_shift((int64_t)d * ms_fres_inv[q], 32);
        v->fres_n = ms_fres_n[q];
    } else {
        v->fres = fres;
        v->fres_n = 0;
    }
    coefs(v, v->fres, q);
    // a block without gain is not filtered (FluidSynth's fluid_rvoice_write())
    v->silent = v->amp == 0 && v->amp_step == 0;
    v->first = 0;
}

// a looping voice past its loop end back by the loop length
static uint32_t wrap(voice_t* v, const mh_midi_params* p) {
    uint32_t idx = (uint32_t)(v->pos >> 32);

    while (idx >= p->loop_end) {
        v->pos -= (uint64_t)(p->loop_end - p->loop_start) << 32;
        idx = (uint32_t)(v->pos >> 32);
        v->looped = 1;
    }
    return idx;
}

// a voice in a frame (docs/midi_synth.md, "The sample engine")
static void run(const midisynth* ms, voice_t* v, int64_t* sums) {
    const mh_midi_params* p = &v->p;
    uint32_t idx = (uint32_t)(v->pos >> 32), last, first;
    int looping = (p->loop_mode == 1 || (p->loop_mode == 3 && v->env[0].sec < SEC_RELEASE))
        && p->loop_end > p->loop_start;
    int32_t s0, s1, s2, s3, x0, y0, out;
    const int32_t* c;
    int64_t acc;
    int k;

    // a looping voice is inside its loop: a start past the loop's end comes
    // wrapped (with looped set), and step 6 wraps it after every frame
    if (!looping && idx >= p->end) {
        // the sends are at their targets after a block that has sounded
        // (FluidSynth's fluid_rvoice_buffers_mix() of a voice's last block)
        if (v->ran > 0) {
            for (k = 0; k < 4; k++) {
                v->send[k] += v->send_step[k] * (MH_MIDI_BLOCK - v->ran);
            }
        }
        v->on = 0;
        return;
    }

    // 1. the four samples around the position (FluidSynth's rules)
    first = v->looped ? p->loop_start : p->start;
    s0 = idx <= first ? sample(ms, v->looped ? p->loop_end - 1 : p->start) : sample(ms, idx - 1);
    s1 = sample(ms, idx);
    if (looping) {
        s2 = sample(ms, idx + 1 < p->loop_end ? idx + 1 : p->loop_start + (idx + 1 - p->loop_end));
        s3 = sample(ms, idx + 2 < p->loop_end ? idx + 2 : p->loop_start + (idx + 2 - p->loop_end));
    } else {
        last = p->end - 1;
        s2 = sample(ms, idx + 1 < last ? idx + 1 : last);
        s3 = sample(ms, idx + 2 < last ? idx + 2 : last);
    }

    // 2. cubic interpolation, Q.4
    c = &ms_interp[((v->pos >> 24) & 0xff) * 4];
    acc = (int64_t)c[0] * s0 + (int64_t)c[1] * s1 + (int64_t)c[2] * s2 + (int64_t)c[3] * s3;
    x0 = (int32_t)floor_shift(acc, 12);

    // 3. the filter, direct form II as FluidSynth's (the two differ while the
    // cutoff moves); then the cutoff on to its next frame
    if (v->silent) {
        y0 = 0;
    } else {
        __int128 cn, yy;

        cn = (__int128)x0 - (((__int128)v->coef[2] * v->h1 + (__int128)v->coef[3] * v->h2) >> 24);
        cn = cn > H_MAX ? H_MAX : cn < -H_MAX - 1 ? -H_MAX - 1 : cn;
        yy = ((__int128)v->coef[0] * (cn + v->h2) + (__int128)v->coef[1] * v->h1) >> 24;
        y0 = yy > Y_MAX ? Y_MAX : yy < -Y_MAX - 1 ? -Y_MAX - 1 : (int32_t)yy;
        v->h2 = v->h1;
        v->h1 = (int64_t)cn;
        if (v->fres_n > 0) {
            v->fres += v->fres_incr;
            v->fres_n--;
            coefs(v, v->fres, p->q > 960 ? 960 : (int32_t)p->q);
        }
    }

    // 4. the gain
    out = (int32_t)floor_shift((int64_t)y0 * v->amp, 26);
    v->amp += v->amp_step;
    v->amp = v->amp < 0 ? 0 : v->amp > AMP_MAX ? AMP_MAX : v->amp;

    // 5. the mix, Q.14
    for (k = 0; k < 4; k++) {
        sums[k] += floor_shift((int64_t)out * v->send[k], 12);
        v->send[k] += v->send_step[k];
    }
    v->ran++;

    // 6. on; a looping voice back into its loop at once, also after the
    // block's last frame (FluidSynth's fluid_rvoice_dsp_interpolate_*()):
    // a voice released there leaves the loop from inside it
    v->pos += v->incr;
    if (looping) {
        wrap(v, p);
    }
}

static void start(voice_t* v, const mh_midi_params* p) {
    int32_t send[4], other[4];

    // the sends go on from where the voice's last note left them, as
    // FluidSynth's rvoice buffers do (0 after a reset). A voice has two of
    // them: a note that cuts one still playing goes on the other (FluidSynth's
    // overflow rvoice), with what that one's last note left.
    if (v->on) {
        memcpy(send, v->send_other, sizeof(send));
        memcpy(other, v->send, sizeof(other));
    } else {
        memcpy(send, v->send, sizeof(send));
        memcpy(other, v->send_other, sizeof(other));
    }
    memset(v, 0, sizeof(*v));
    memcpy(v->send, send, sizeof(send));
    memcpy(v->send_other, other, sizeof(other));
    v->p = *p;
    v->on = 1;
    v->due = 1;
    v->first = 1;
    v->looped = (int)p->looped;
    v->pos = (uint64_t)p->start << 32;
}

// an UPDATE carries the voice's whole block, only the fields marked U in
// docs/midi_synth.md changed (the engine takes all of it)
static void update(voice_t* v, const mh_midi_params* p) {
    v->p = *p;
}

// The effects as after a reset: every line silent
static void fx_reset(fx_t* f) {
    int n, at = 0;

    memset(f, 0, sizeof(*f));
    for (n = 0; n < REV_LINES; n++) {
        f->base[n] = (uint16_t)at;
        at += rev_len[n];
    }
    // as FluidSynth's: the centre the depth and one sample behind the input,
    // and the LFOs a third of a turn apart, stepped at the first sample
    f->center = MS_CHO_SIZE - 1 - MS_CHO_DEPTH;
    f->rate = MS_CHO_RATE;
    f->phase[1] = 0x555555555555ull;
    f->phase[2] = 0xAAAAAAAAAAABull;
}

// v >> n, rounded to the nearest (half up)
static int64_t fx_round(int64_t v, int n) {
    return floor_shift(v + (1ll << (n - 1)), n);
}

static int32_t fx_hold(int64_t v) {
    return (int32_t)(v > FX_MAX ? FX_MAX : v < -FX_MAX - 1 ? -FX_MAX - 1 : v);
}

// held at `bits` bits, signed: the RTL's registers
static int64_t fx_held(int64_t v, int bits) {
    const int64_t lim = (1ll << (bits - 1)) - 1;

    return v > lim ? lim : v < -lim - 1 ? -lim - 1 : v;
}

// sin of a phase (Q.32 of a turn), Q.18: a quarter wave, interpolated
static int32_t fx_sine(uint32_t ph) {
    uint32_t x = ph & 0x3FFFFFFFu;
    int32_t v;
    int i;

    if (ph & 0x40000000u) {
        x = 0x40000000u - x;
    }
    i = (int)(x >> 20);
    v = ms_fxsin[i];
    if (i < 1024) {
        v += (int32_t)floor_shift((int64_t)(ms_fxsin[i + 1] - ms_fxsin[i]) * (x & 0xFFFFF), 20);
    }
    return ph & 0x80000000u ? -v : v;
}

midisynth* ms_new(const int16_t* samples, uint32_t nsamples) {
    midisynth* ms = calloc(1, sizeof(*ms));

    if (ms == NULL) {
        return NULL;
    }
    ms->q = calloc(MH_MIDI_RING_CMDS, sizeof(*ms->q));
    if (ms->q == NULL) {
        free(ms);
        return NULL;
    }
    ms->smp = samples;
    ms->nsmp = nsamples;
    fx_reset(&ms->fx);
    return ms;
}

void ms_free(midisynth* ms) {
    if (ms != NULL) {
        free(ms->q);
        free(ms);
    }
}

void ms_reset(midisynth* ms) {
    memset(ms->v, 0, sizeof(ms->v));
    ms->qhead = ms->qcount = 0;
    fx_reset(&ms->fx);
}

int ms_push(midisynth* ms, const uint64_t* cmd) {
    if (ms->qcount >= MH_MIDI_RING_CMDS) {
        return 0;
    }
    memcpy(ms->q[(ms->qhead + ms->qcount) % MH_MIDI_RING_CMDS], cmd, sizeof(ms->q[0]));
    ms->qcount++;
    return 1;
}

int ms_queued(const midisynth* ms) {
    return ms->qcount;
}

void ms_frame(midisynth* ms, uint32_t frame, int64_t* sums) {
    int made = 0, starts = 0, n, voice, command;
    uint32_t at;
    mh_midi_params p;
    voice_t* v;

    // 1. the commands of this frame and before, in the order written (all of
    // them when aligned with FluidSynth, which has no such limits)
    while (ms->qcount > 0 && (made < CMDS_A_FRAME || ms->aligned)) {
        const uint64_t* cmd = ms->q[ms->qhead];

        mh_midi_unpack(cmd, &at, &voice, &command, &p);
        if ((int32_t)(at - frame) > 0) {
            break;
        }
        if (command == MH_MIDI_START && starts == STARTS_A_FRAME && !ms->aligned) {
            break;
        }
        v = &ms->v[voice];
        switch (command) {
        case MH_MIDI_START:
            start(v, &p);
            starts++;
            break;
        case MH_MIDI_UPDATE:
            if (v->on) {
                update(v, &p);
            }
            break;
        case MH_MIDI_RELEASE:
            if (v->on && v->release == 0) {
                v->release = 1;
            }
            break;
        case MH_MIDI_KILL:
            if (v->on) {
                v->release = 2;
            }
            break;
        case MH_MIDI_STOP:
            v->on = 0;
            break;
        default:
            break;
        }
        ms->qhead = (ms->qhead + 1) % MH_MIDI_RING_CMDS;
        ms->qcount--;
        made++;
    }

    // 2. the control updates: the voices started now, and two by turns
    for (n = 0; n < MH_MIDI_VOICES; n++) {
        v = &ms->v[n];
        uint32_t phase = ms->aligned ? 0 : (uint32_t)n & (MH_MIDI_BLOCK - 1);

        if (v->on && (v->due || phase == (frame & (MH_MIDI_BLOCK - 1)))) {
            v->due = 0;
#ifdef MS_CONTROL_HOOK
            {
                voice_t before = *v;

                control(v);
                MS_CONTROL_HOOK(&before, v);
            }
#else
            control(v);
#endif
        }
    }

    // 3. every voice
    sums[0] = sums[1] = sums[2] = sums[3] = 0;
    for (n = 0; n < MH_MIDI_VOICES; n++) {
        if (ms->v[n].on) {
            run(ms, &ms->v[n], sums);
        }
    }
    for (n = 0; n < 4; n++) {
        const int64_t lim = (1ll << 39) - 1;

        sums[n] = sums[n] > lim ? lim : sums[n] < -lim - 1 ? -lim - 1 : sums[n];
    }
}

void ms_effects(midisynth* ms, int64_t* sums) {
    fx_t* f = &ms->fx;
    int32_t* cho = f->mem + FX_WORDS - MS_CHO_SIZE;
    int64_t x, side[2], d[2] = {0, 0}, o = 0, wet[2];
    int n, k, step;

    // the reverb: in parallel the eight combs of a side, then in series its
    // four allpasses
    x = fx_hold(fx_round(sums[2] * MS_REV_IN, 16 + REV_SHIFT));
    for (k = 0; k < 2; k++) {
        int64_t acc = 0;

        for (n = 8 * k; n < 8 * k + 8; n++) {
            int32_t* w = &f->mem[f->base[n] + f->ptr[n]];
            int32_t b = *w;

            f->store[n] = (int32_t)fx_round((int64_t)b * MS_REV_DAMP2 + (int64_t)f->store[n] * MS_REV_DAMP1, 15);
            *w = fx_hold(x + fx_round((int64_t)f->store[n] * MS_REV_FB, 15));
            FX_PEAK(0, x + fx_round((int64_t)f->store[n] * MS_REV_FB, 15));
            acc += b;
        }
        for (n = 16 + 4 * k; n < 20 + 4 * k; n++) {
            int32_t* w = &f->mem[f->base[n] + f->ptr[n]];
            int32_t b = *w;

            // Freeverb's allpass: feedback 0.5, the output the delay's less
            // the input. Its line holds half the value: the sum of eight
            // combs comes near the 24 bits in a loud song (The HYBRID
            // Collage at 0.92 of them)
            int64_t in = acc;

            *w = fx_hold(fx_round(in + b, 1));
            FX_PEAK(1, fx_round(in + b, 1));
            acc = fx_held(2 * (int64_t)b - in, 27);
        }
        FX_PEAK(3, acc);
        side[k] = acc;
    }
    for (n = 0; n < REV_LINES; n++) {
        f->ptr[n] = (uint16_t)(f->ptr[n] + 1 == rev_len[n] ? 0 : f->ptr[n] + 1);
    }
    wet[0] = fx_round(side[0] * MS_REV_WET1 + side[1] * MS_REV_WET2, 15) * (1 << REV_SHIFT);
    wet[1] = fx_round(side[1] * MS_REV_WET1 + side[0] * MS_REV_WET2, 15) * (1 << REV_SHIFT);

    // the chorus: three blocks reading the line at a position an LFO
    // moves, first-order allpass interpolated; the LFOs and the centre
    // stepped every MS_CHO_RATE samples
    step = ++f->rate >= MS_CHO_RATE;
    for (n = 0; n < 3; n++) {
        int at;

        if (step) {
            int64_t pos;

            f->phase[n] = (f->phase[n] + MS_CHO_STEP) & ((1ull << 48) - 1);
            pos = ((int64_t)f->center << 18) + (int64_t)fx_sine((uint32_t)(f->phase[n] >> 16)) * MS_CHO_DEPTH;
            at = (int)(pos >> 18);
            f->frac[n] = (int32_t)(pos & 0x3FFFF);
            f->out[n] = at < 0 ? at + MS_CHO_SIZE : at >= MS_CHO_SIZE ? at - MS_CHO_SIZE : at;
        }
        at = f->out[n];
        o = cho[at];
        at = at + 1 == MS_CHO_SIZE ? 0 : at + 1;
        f->out[n] = at;
        o = fx_held(o + fx_round(f->frac[n] * (cho[at] - f->last[n]), 18), 26);
        f->last[n] = o;
        d[n & 1] += o;
    }
    if (step) {
        f->rate = 0;
        f->center += MS_CHO_RATE;
        if (f->center >= MS_CHO_SIZE) {
            f->center -= MS_CHO_SIZE;
        }
    }
    // FluidSynth's balance of an odd number of blocks: the last one on the
    // right too
    d[1] += o;
    cho[f->cin] = fx_hold(fx_round(sums[3], CHO_SHIFT));
    FX_PEAK(2, fx_round(sums[3], CHO_SHIFT));
    f->cin = f->cin + 1 == MS_CHO_SIZE ? 0 : f->cin + 1;
    wet[0] += fx_round(d[0] * MS_CHO_WET1 + d[1] * MS_CHO_WET2, 15) * (1 << CHO_SHIFT);
    wet[1] += fx_round(d[1] * MS_CHO_WET1 + d[0] * MS_CHO_WET2, 15) * (1 << CHO_SHIFT);

    for (k = 0; k < 2; k++) {
        const int64_t lim = (1ll << 39) - 1;
        int64_t s = sums[k] + wet[k];

        sums[k] = s > lim ? lim : s < -lim - 1 ? -lim - 1 : s;
    }
}

void ms_output(const int64_t* sums, int volume, int16_t* out) {
    int k;

    for (k = 0; k < 2; k++) {
        // 32 is FluidSynth's synth.gain of 0.5: Q.14, times 0.5 / 32
        int64_t s = floor_shift(sums[k] * volume, 20);

        out[k] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}

void ms_aligned(midisynth* ms, int aligned) {
    ms->aligned = aligned;
}

int ms_playing(const midisynth* ms, uint32_t* bits) {
    int n, count = 0;

    bits[0] = bits[1] = bits[2] = bits[3] = 0;
    for (n = 0; n < MH_MIDI_VOICES; n++) {
        if (ms->v[n].on) {
            bits[n >> 5] |= 1u << (n & 31);
            count++;
        }
    }
    return count;
}
