// The C model with a look at every control update (MS_CONTROL_HOOK), for
// sim/midi_unit.cpp: the voice before and after goes to the RTL there.
#define MS_CONTROL_HOOK unit_control
#include "../tools/midisynth/midisynth.c"
#include "midi_unit.h"

static void unit_control(const voice_t* b, const voice_t* a) {
    unit_in in;
    unit_out out;
    int k;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    mh_midi_pack(in.w, 0, 0, 0, &b->p);
    for (k = 0; k < 2; k++) {
        in.sec[k] = b->env[k].sec;
        in.cnt[k] = b->env[k].cnt;
        in.x[k] = (uint64_t)b->env[k].x;
        in.lfo[k] = b->lfo[k].val;
        in.down[k] = b->lfo[k].down;
    }
    in.blocks = b->blocks;
    in.release = b->release;
    in.killed = b->killed;
    in.first = b->first;
    in.looped = b->looped;
    in.amp = b->amp;
    for (k = 0; k < 4; k++) {
        in.send[k] = b->send[k];
    }
    in.fres = b->fres;

    out.off = !a->on;
    out.released = b->release != 0 && a->release == 0;
    for (k = 0; k < 2; k++) {
        out.sec[k] = a->env[k].sec;
        out.cnt[k] = a->env[k].cnt;
        out.x[k] = (uint64_t)a->env[k].x;
        out.lfo[k] = a->lfo[k].val;
        out.down[k] = a->lfo[k].down;
    }
    out.blocks = a->blocks;
    out.killed = a->killed;
    out.incr = a->incr;
    out.amp_step = a->amp_step;
    for (k = 0; k < 4; k++) {
        out.send_step[k] = a->send_step[k];
        out.coef[k] = a->coef[k];
    }
    out.fres = a->fres;
    out.fres_incr = a->fres_incr;
    out.fres_n = a->fres_n;
    out.silent = a->silent;
    out.q = a->p.q > 960 ? 960 : (int)a->p.q;
    unit_check(&in, &out);
}
