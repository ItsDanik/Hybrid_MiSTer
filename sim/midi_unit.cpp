// The MIDI synthesizer's control engine and filter coefficients (RTL) against
// the C model, update by update, while the model plays a song: every control
// update the model makes is made again by hybrid_midi_ctrl.sv from the same
// voice, and the coefficients of its cutoff by hybrid_midi_coef.sv.
// usage: sim/midi_unit.sh <font.sf2> <song.mid>...
// Prints the updates checked and every field that differs.
#include "Vtb_midi_unit.h"
#include "verilated.h"
#include "midi_unit.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int render_main(int argc, char** argv);

static Vtb_midi_unit* top;
static long checked, bad, coefs_checked;
static long field_bad[32];
static const char* field_name[32] = {
    "off", "released", "sec0", "sec1", "cnt0", "cnt1", "x0", "x1", "lfo0", "lfo1", "down0", "down1",
    "blocks", "killed", "incr", "amp_step", "send_step", "fres", "fres_incr", "fres_n", "silent", "q",
    "coefs",
};

static void tick() {
    top->clk = 0;
    top->eval();
    top->clk = 1;
    top->eval();
}

static void wide(VlWide<3>& w, const uint32_t* v) {
    w[0] = v[0];
    w[1] = v[1];
    w[2] = v[2];
}

static uint64_t mask(uint64_t v, int bits) {
    return bits >= 64 ? v : v & ((1ull << bits) - 1);
}

static int64_t sx(uint64_t v, int bits) {
    return (int64_t)(v << (64 - bits)) >> (64 - bits);
}

static void differ(int f, const char* fmt, long long rtl, long long c) {
    field_bad[f]++;
    if (field_bad[f] <= 5) {
        fprintf(stderr, "update %ld: %s rtl %lld (%s) c %lld\n", checked, field_name[f], rtl, fmt, c);
    }
}

extern "C" void unit_check(const unit_in* in, const unit_out* out) {
    uint32_t send[3];
    int k, ok = 1;

    top->w1 = in->w[1];
    top->w3 = in->w[3];
    top->w4 = in->w[4];
    top->w5 = in->w[5];
    top->w6 = in->w[6];
    top->w7 = in->w[7];
    top->w8 = in->w[8];
    top->w9 = in->w[9];
    top->w10 = in->w[10];
    top->w11 = in->w[11];
    top->w12 = in->w[12];
    top->w13 = in->w[13];
    top->w14 = in->w[14];
    top->w15 = in->w[15];
    top->i_sec0 = in->sec[0];
    top->i_sec1 = in->sec[1];
    top->i_cnt0 = in->cnt[0] > 0xffff ? 0xffff : in->cnt[0];
    top->i_cnt1 = in->cnt[1] > 0xffff ? 0xffff : in->cnt[1];
    top->i_x0 = mask(in->x[0], 40);
    top->i_x1 = mask(in->x[1], 40);
    top->i_lfo0 = mask((uint64_t)in->lfo[0], 33);
    top->i_lfo1 = mask((uint64_t)in->lfo[1], 33);
    top->i_down0 = in->down[0];
    top->i_down1 = in->down[1];
    top->i_blocks = in->blocks > 0xffff ? 0xffff : in->blocks;
    top->i_release = in->release;
    top->i_killed = in->killed;
    top->i_first = in->first;
    top->i_looped = in->looped;
    top->i_amp = in->amp;
    send[0] = (uint32_t)(in->send[0] & 0xffffff) | (uint32_t)in->send[1] << 24;
    send[1] = (uint32_t)(in->send[1] & 0xffffff) >> 8 | (uint32_t)in->send[2] << 16;
    send[2] = (uint32_t)(in->send[2] & 0xffffff) >> 16 | (uint32_t)in->send[3] << 8;
    wide(top->i_send, send);
    top->i_fres = (uint32_t)in->fres;

    top->go = 1;
    tick();
    top->go = 0;
    for (k = 0; !top->done; k++) {
        tick();
        if (k > 100) {
            fprintf(stderr, "the control engine never ends\n");
            exit(1);
        }
    }
    checked++;

#define CMP(f, rtl, c) do { if ((long long)(rtl) != (long long)(c)) { differ(f, #rtl, (long long)(rtl), (long long)(c)); ok = 0; } } while (0)
    CMP(0, top->o_off, out->off);
    if (!out->off) {
        CMP(1, top->o_released, out->released);
        CMP(2, top->o_sec0, out->sec[0]);
        CMP(3, top->o_sec1, out->sec[1]);
        CMP(4, top->o_cnt0, mask(out->cnt[0], 16));
        CMP(5, top->o_cnt1, mask(out->cnt[1], 16));
        CMP(6, top->o_x0, out->x[0]);
        CMP(7, top->o_x1, out->x[1]);
        CMP(8, sx(top->o_lfo0, 33), out->lfo[0]);
        CMP(9, sx(top->o_lfo1, 33), out->lfo[1]);
        CMP(10, top->o_down0, out->down[0]);
        CMP(11, top->o_down1, out->down[1]);
        CMP(12, top->o_blocks, out->blocks > 0xffff ? 0xffff : out->blocks);
        CMP(13, top->o_killed, out->killed);
        CMP(14, top->o_incr, out->incr);
        CMP(15, sx(top->o_amp_step, 30), out->amp_step);
        for (k = 0; k < 4; k++) {
            uint64_t s = (uint64_t)top->o_send_step[0] | (uint64_t)top->o_send_step[1] << 32;
            uint64_t s2 = (uint64_t)top->o_send_step[1] >> 0 | (uint64_t)top->o_send_step[2] << 32;
            uint64_t v = k < 2 ? s >> (24 * k) : s2 >> (24 * k - 32);
            CMP(16, sx(v & 0xffffff, 24), out->send_step[k]);
        }
        CMP(17, (int32_t)top->o_fres, out->fres);
        if (out->fres_n > 0) {
            CMP(18, (int32_t)top->o_fres_incr, out->fres_incr);
        }
        CMP(19, top->o_fres_n, out->fres_n);
        CMP(20, top->o_silent, out->silent);
        CMP(21, top->o_q, out->q);

        // the coefficients of the cutoff the voice has now
        top->job = 1;
        top->job_voice = 5;
        top->job_fres = (uint32_t)out->fres;
        top->job_q = out->q;
        tick();
        top->job = 0;
        for (k = 0; !top->wr; k++) {
            tick();
            if (k > 100) {
                fprintf(stderr, "the coefficient unit never ends\n");
                exit(1);
            }
        }
        coefs_checked++;
        if (sx(top->wr_b02, 27) != out->coef[0] || sx(top->wr_b1, 27) != out->coef[1]
            || sx(top->wr_a1, 27) != out->coef[2] || sx(top->wr_a2, 27) != out->coef[3]) {
            field_bad[22]++;
            ok = 0;
            if (field_bad[22] <= 5) {
                fprintf(stderr, "update %ld: coefs of %d q %d: rtl %lld %lld %lld %lld c %d %d %d %d\n", checked,
                    out->fres, out->q, (long long)sx(top->wr_b02, 27), (long long)sx(top->wr_b1, 27),
                    (long long)sx(top->wr_a1, 27), (long long)sx(top->wr_a2, 27), out->coef[0], out->coef[1],
                    out->coef[2], out->coef[3]);
            }
        }
        while (!top->idle) {
            tick();
        }
    }
    bad += !ok;
}

int main(int argc, char** argv) {
    const char* args[5];
    int r = 0, f, n;

    if (argc < 3) {
        fprintf(stderr, "usage: midi_unit <font.sf2> <song.mid>...\n");
        return 1;
    }
    Verilated::commandArgs(argc, argv);
    top = new Vtb_midi_unit;
    for (r = 0; r < 4; r++) {
        tick();
    }
    for (n = 2; n < argc; n++) {
        args[0] = "render";
        args[1] = argv[1];
        args[2] = argv[n];
        args[3] = "/dev/null";
        args[4] = NULL;
        r |= render_main(4, (char**)args);
    }
    printf("control updates checked %ld, coefficients %ld, different %ld\n", checked, coefs_checked, bad);
    for (f = 0; f < 23; f++) {
        if (field_bad[f]) {
            printf("  %s: %ld\n", field_name[f], field_bad[f]);
        }
    }
    delete top;
    return r != 0 || bad != 0;
}
