#!/usr/bin/env python3
# The tables of the MIDI synthesizer's voice engine (docs/midi_synth.md,
# "Design"), for the C model (midisynth_tables.h) and, with --hex <dir>, for
# the RTL ($readmemh files). The values here are the definition: the C model
# and the RTL read the same numbers. Most follow FluidSynth 2.6.1, whose
# formulas are named with each table.
#
# usage: mktables.py <out.h> [--hex <dir>]
import math, os, sys

RATE = 44100

def interp():
    # FluidSynth's gentables/fluid_interp_coeff.cpp: Catmull-Rom, 256 rows,
    # Q1.16 (1.0 = 65536)
    rows = []
    for i in range(256):
        x = i / 256.0
        c = (x * (-0.5 + x * (1 - 0.5 * x)),
             1.0 + x * x * (1.5 * x - 2.5),
             x * (0.5 + x * (2.0 - 1.5 * x)),
             0.5 * x * x * (x - 1.0))
        rows += [round(v * 65536) for v in c]
    return rows

def pitch():
    # 2^(i/1200), Q1.32 (FluidSynth's phase is 32.32, its increment worked
    # out in double)
    return [round(2 ** (i / 1200.0) * (1 << 32)) for i in range(1200)]

def amp():
    # 2^(-i/1024), Q0.26 (1.0 = 1 << 26)
    return [round(2 ** (-i / 1024.0) * (1 << 26)) for i in range(1024)]

def cb2amp():
    # FluidSynth's gentables/fluid_cb2amp.cpp: whole cB 0..1440, Q0.26
    return [round(10 ** (i / -200.0) * (1 << 26)) for i in range(1441)]

def log2t():
    # log2(1 + i/1024), Q0.16
    return [round(math.log2(1 + i / 1024.0) * 65536) for i in range(1024)]

def convex():
    # FluidSynth's gentables/fluid_convex.cpp, Q1.23
    out = []
    for i in range(128):
        if i == 0:
            v = 0.0
        elif i == 127:
            v = 1.0
        else:
            v = 1.0 - ((-200.0 * 2 / 960.0) * math.log(i / 127.0) / math.log(10))
        out.append(round(v * (1 << 23)))
    return out

FC_MIN, FC_MAX = 1500, 13500

def sincos():
    # the filter's sin and cos of 2 pi f / 44100, f of 1500..13500 cents in
    # whole cents (FluidSynth's table), Q1.24 signed
    s, c = [], []
    for i in range(FC_MAX - FC_MIN + 1):
        f = 440.0 * 2 ** ((FC_MIN + i - 6900) / 1200.0)
        w = 2 * math.pi * f / RATE
        s.append(round(math.sin(w) * (1 << 24)))
        c.append(round(math.cos(w) * (1 << 24)))
    return s, c

def qtabs():
    # FluidSynth's fluid_iir_filter_q_from_dB(): q = 10^((Q/10 - 3.01) / 20),
    # Q in cB 0..960. 1/(2q) Q0.24 and 1/sqrt(q) Q1.24.
    inv2q, isq = [], []
    for cb in range(961):
        q = 10 ** ((cb / 10.0 - 3.01) / 20.0)
        inv2q.append(round(1 / (2 * q) * (1 << 24)))
        isq.append(round(1 / math.sqrt(q) * (1 << 24)))
    return inv2q, isq

def fres_ramp():
    # FluidSynth's fluid_iir_filter_calc(): a jump of the cutoff of more than
    # a cent is spread over 64 * clip(q, 1, 5) frames, so that a resonant
    # filter does not click. Frames (rounded) and 2^32 / (64 * clip(q, 1, 5))
    # by Q in cB.
    n, inv = [], []
    for cb in range(961):
        q = 10 ** ((cb / 10.0 - 3.01) / 20.0)
        b = 64 * min(max(q, 1.0), 5.0)
        n.append(int(b + 0.5))
        inv.append(round((1 << 32) / b))
    return n, inv

def fxsin():
    # a quarter of a sine wave, 1025 points, Q0.18: the chorus's LFOs,
    # interpolated
    return [round(math.sin(i / 1024.0 * math.pi / 2) * (1 << 18)) for i in range(1025)]

def fx_consts():
    # FluidSynth 2.6.1's reverb "free" (Freeverb, fluid_rev_freeverb.cpp) and
    # its chorus (fluid_chorus.c) with their defaults (fluid_synth.h): room
    # size 0.5, damping 0.2, width 0.8, level 0.7; 3 blocks, level 0.6,
    # speed 0.2 Hz, depth 4.25 ms, sine
    room, damp, width, level = 0.5, 0.2, 0.8, 0.7
    wet = level * 3.0 / (1.0 + width * 0.2)
    c_level, c_speed, c_depth, c_width = 0.6, 0.2, 4.25, 10
    c_wet = c_level / (1.0 + c_width * 0.2)
    mod_depth = int(c_depth / 1000.0 * RATE) // 2
    mod_rate = 5
    damp1 = round(damp * 32768)
    return {
        # the reverb's input: Freeverb's fixed gain 0.015 of twice the mono input, Q.16
        'MS_REV_IN': round(2 * 0.015 * 65536),
        # the combs: feedback (room size), damping and its complement, Q.15
        'MS_REV_FB': round((room * 0.28 + 0.7) * 32768),
        'MS_REV_DAMP1': damp1,
        'MS_REV_DAMP2': 32768 - damp1,
        # the output: wet1 and wet2, Q.15
        'MS_REV_WET1': round(wet * (width / 2 + 0.5) * 32768),
        'MS_REV_WET2': round(wet * ((1 - width) / 2) * 32768),
        # the chorus: its delay line (2048 samples and 1 for the
        # interpolation), half the modulation's depth in samples, the
        # modulation's rate (an LFO step every 5 samples)
        'MS_CHO_SIZE': 2048 + 1,
        'MS_CHO_DEPTH': mod_depth,
        'MS_CHO_RATE': mod_rate,
        # an LFO's step at each of its updates, Q.48 of a turn (with Q.32
        # the rounding of the step moved the position 0.09 samples off
        # FluidSynth's in three minutes)
        'MS_CHO_STEP': round(c_speed * mod_rate / RATE * (1 << 48)),
        # the output: wet1 and wet2, Q.15
        'MS_CHO_WET1': round(c_wet * (c_width / 2 + 0.5) * 32768),
        'MS_CHO_WET2': round(c_wet * ((1 - c_width) / 2) * 32768),
    }

def consts():
    max_fc = 6900 + 1200 * math.log2(0.45 * RATE / 440.0)
    # the exclusive class's release (FluidSynth: -2000 timecents)
    kill_blocks = 1 + int(RATE * 2 ** (-2000 / 1200.0) / 64)
    return {
        # cb2amp: 1/16 cB to 1/1024 octaves, Q0.16: log2(10) / 3200 * 1024
        'MS_K_CB': round(math.log2(10) / 3200 * 1024 * 65536),
        # volume envelope from the attack to the release: x = 1 + 200 / 960 * log10(x),
        # the factor of log2(x), Q.23
        'MS_K_REL': round(200 / 960.0 * math.log10(2) * (1 << 23)),
        'MS_KILL_DEC': round((1 << 39) / kill_blocks),
        # the cutoff's table starts at 1500 cents
        'MS_FC_MIN': FC_MIN,
        # the cutoff's limits in cents, Q.16: 5 Hz and 0.45 * 44.1 kHz
        # (FluidSynth's fluid_iir_filter_calc())
        'MS_FRES_LO': round((6900 + 1200 * math.log2(5 / 440.0)) * 65536),
        'MS_FRES_HI': round(max_fc * 65536),
    }

# the widths of the RTL's tables: signed where a value can be negative
HEX_BITS = {'ms_interp': 18, 'ms_pitch': 33, 'ms_amp': 27, 'ms_cb2amp': 27, 'ms_log2': 16,
            'ms_convex': 24, 'ms_sin': 24, 'ms_cos': 25, 'ms_inv2q': 24, 'ms_isq': 25,
            'ms_fres_n': 9, 'ms_fres_inv': 27, 'ms_fxsin': 19}

def c_array(name, ctype, vals, per=12):
    lines = ['static const %s %s[%d] = {' % (ctype, name, len(vals))]
    for i in range(0, len(vals), per):
        lines.append('    ' + ', '.join(str(v) for v in vals[i:i + per]) + ',')
    lines.append('};')
    return '\n'.join(lines)

def main():
    if len(sys.argv) < 2:
        sys.exit('usage: mktables.py <out.h> [--hex <dir>]')
    s, c = sincos()
    inv2q, isq = qtabs()
    rn, rinv = fres_ramp()
    tables = [
        ('ms_interp', 'int32_t', interp()),
        ('ms_pitch', 'uint64_t', pitch()),
        ('ms_amp', 'uint32_t', amp()),
        ('ms_cb2amp', 'uint32_t', cb2amp()),
        ('ms_log2', 'uint32_t', log2t()),
        ('ms_convex', 'int32_t', convex()),
        ('ms_sin', 'int32_t', s),
        ('ms_cos', 'int32_t', c),
        ('ms_inv2q', 'int32_t', inv2q),
        ('ms_isq', 'int32_t', isq),
        ('ms_fres_n', 'uint16_t', rn),
        ('ms_fres_inv', 'uint32_t', rinv),
        ('ms_fxsin', 'int32_t', fxsin()),
    ]
    out = ['// Made by tools/midisynth/mktables.py: do not edit.',
           '#ifndef MIDISYNTH_TABLES_H', '#define MIDISYNTH_TABLES_H', '#include <stdint.h>', '']
    for k, v in list(consts().items()) + list(fx_consts().items()):
        out.append('#define %s %d' % (k, v))
    out.append('')
    for name, ctype, vals in tables:
        out.append(c_array(name, ctype, vals))
        out.append('')
    out.append('#endif')
    with open(sys.argv[1], 'w') as f:
        f.write('\n'.join(out) + '\n')
    if '--hex' in sys.argv:
        d = sys.argv[sys.argv.index('--hex') + 1]
        os.makedirs(d, exist_ok=True)
        # each in the width the RTL reads it with (two's complement), as
        # rtl/hybrid_midi*.sv declares it; the interpolation's four
        # coefficients of a row in one entry, the first in the low bits
        for name, ctype, vals in tables:
            bits = HEX_BITS[name]
            assert all(-(1 << (bits - 1)) <= v < (1 << bits) for v in vals), name
            vals = [v & ((1 << bits) - 1) for v in vals]
            if name == 'ms_interp':
                vals = [sum(vals[4 * r + k] << (bits * k) for k in range(4)) for r in range(len(vals) // 4)]
                bits *= 4
            if name == 'ms_fxsin':
                # the last point, 1.0, is a constant in rtl/hybrid_midi_fx.sv
                assert vals[1024] == 1 << 18
                vals = vals[:1024]
            with open(os.path.join(d, name + '.hex'), 'w') as f:
                for v in vals:
                    f.write('%0*x\n' % ((bits + 3) // 4, v))

main()
