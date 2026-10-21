// The MIDI synthesizer's RTL (rtl/hybrid_midi.sv, in Verilator) against its C
// model, frame by frame: the library plays a song with a SoundFont
// (midi_engine_gen.c), and the commands it sent go to both, the RTL through
// a model of the host's DDR3 reads (latency, other reads in between). Each
// frame's four sums must be the same.
// usage: sim/midi_engine.sh <font.sf2> <song.mid> [seconds] [options]
//   seconds  of the song (0: all of it)
//   options  fast: the next frame as soon as the engine has made one (for
//            long tests; the default is the real 44.1kHz)
//            ahead=N: commands visible N frames before they were sent
//            (default 64)
//            lat=N: the DDR3's latency in clocks of 50MHz (default 12, and
//            now and then a video line's read before)
// Prints the frames compared, those that differ, the cache's underruns and
// the frames the engine was late for.
#include "Vhybrid_midi.h"
#include "verilated.h"
#include "midi_engine.h"
extern "C" {
#include "../tools/midisynth/midisynth.h"
}
#include "../hps/mister_midi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static Vhybrid_midi* top;
static uint64_t t5;                  // time in 5ns steps

// one 5ns step: mclk every 10ns, clk every 20ns
static void step() {
    top->mclk = (t5 & 1) == 0;
    top->clk = (t5 & 3) < 2;
    top->eval();
    t5++;
}

// to the next rising edge of clk
static void clk_cycle() {
    do {
        step();
    } while ((t5 & 3) != 0);
}

static int64_t s40(const VlWide<5>& w, int k) {
    // bits 40k .. 40k + 39
    uint64_t v = 0;
    for (int b = 0; b < 40; b++) {
        int bit = 40 * k + b;
        v |= (uint64_t)((w[bit >> 5] >> (bit & 31)) & 1) << b;
    }
    return (int64_t)(v << 24) >> 24;
}

int main(int argc, char** argv) {
    double seconds = argc > 3 ? atof(argv[3]) : 10;
    int fast = 0, ahead = 64, lat = 12;
    uint32_t frames;
    long n, visible = 0, cmd_next = 0;
    const gen_cmd_t* cmds;
    midisynth* ms;
    uint32_t cf = 0;                  // the C model's next frame
    long compared = 0, differ = 0, late = 0;
    int64_t sums[4];
    int dbg_tgl = 0, frame_now = -1, waiting = 0;
    uint32_t aud_acc = 0, gap = 0;
    int busy = 0, beats_left = 0, delay = 0, kind = 0;
    uint32_t rd_addr = 0;
    long cmd_reads = 0, smp_reads = 0;
    uint32_t seed = 12345;
    uint64_t stb_t = 0, busy_max = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: midi_engine <font.sf2> <song.mid> [seconds] [fast] [ahead=N] [lat=N]\n");
        return 1;
    }
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "fast")) fast = 1;
        else if (!strncmp(argv[i], "ahead=", 6)) ahead = atoi(argv[i] + 6);
        else if (!strncmp(argv[i], "lat=", 4)) lat = atoi(argv[i] + 4);
    }
    n = gen_song(argv[1], argv[2], seconds, &frames);
    if (n < 0) return 1;
    cmds = gen_cmds();
    ms = ms_new(gen_samples(), MH_MIDI_MAX_SAMPLES);
    printf("%s: %u frames, %ld commands\n", argv[2], frames, n);

    Verilated::commandArgs(argc, argv);
    top = new Vhybrid_midi;
    top->en = 0;
    for (int i = 0; i < 40; i++) clk_cycle();
    top->en = 1;
    for (int i = 0; i < 300; i++) clk_cycle();

    // frame_now: the frame the engine is making (-1 before the first)
    while (cf < frames) {
        // ---- the host, at a rising edge of clk: inputs for the next one
        top->frame_stb = 0;
        top->cmd_ack = 0;
        top->smp_ack = 0;
        top->beat = 0;

        // commands the game has written
        while (visible < n && (int64_t)cmds[visible].sent - ahead <= (int64_t)frame_now + 1) {
            ms_push(ms, cmds[visible].w);
            visible++;
        }
        top->wcount = (uint32_t)visible;
        top->volume = 32;

        // the audio frames
        if (fast) {
            if (waiting && (int)top->dbg_tgl != dbg_tgl) waiting = 0;
            if (!waiting && ++gap >= 60) {
                gap = 0;
                waiting = 1;
                top->frame_stb = 1;
                stb_t = t5;
                top->frame = (uint32_t)frame_now;   // the engine makes frame + 1
                frame_now++;
            }
        } else {
            aud_acc += 441;
            if (aud_acc >= 500000) {
                aud_acc -= 500000;
                top->frame_stb = 1;
                stb_t = t5;
                top->frame = (uint32_t)frame_now;
                frame_now++;
            }
        }

        // the DDR3: one read at a time, its words a while after
        if (!busy) {
            if (top->smp_req) {
                top->smp_ack = 1;
                busy = 1;
                kind = 1;
                rd_addr = top->smp_addr;
                beats_left = top->smp_len;
                smp_reads++;
            } else if (top->cmd_req) {
                top->cmd_ack = 1;
                busy = 1;
                kind = 0;
                if (top->cmd_addr != (uint32_t)((cmd_next % 1024) * 16)) {
                    fprintf(stderr, "command read at %u, %ld expected\n", top->cmd_addr, cmd_next);
                    return 1;
                }
                rd_addr = 0;
                beats_left = 16;
                cmd_reads++;
            }
            if (busy) {
                seed = seed * 1103515245 + 12345;
                delay = lat + (int)((seed >> 16) % 8);
                // now and then a video line's read first
                if (((seed >> 8) & 15) == 0) delay += 30 + (int)((seed >> 20) % 50);
            }
        } else if (delay > 0) {
            delay--;
        } else if (beats_left > 0) {
            top->beat = 1;
            if (kind == 0) {
                top->dout = cmds[cmd_next].w[rd_addr];
            } else {
                const int16_t* s = gen_samples() + 4 * (uint64_t)rd_addr;
                top->dout = (uint64_t)(uint16_t)s[0] | (uint64_t)(uint16_t)s[1] << 16 | (uint64_t)(uint16_t)s[2] << 32
                    | (uint64_t)(uint16_t)s[3] << 48;
            }
            rd_addr++;
            if (--beats_left == 0) {
                busy = 0;
                if (kind == 0) cmd_next++;
            }
        }

        clk_cycle();

        // ---- a frame made: the C model's
        if ((int)top->dbg_tgl != dbg_tgl) {
            uint32_t fr = top->dbg_frame;

            dbg_tgl = top->dbg_tgl;
            // clocks of 100MHz from the strobe to the frame made
            if ((t5 - stb_t) / 2 > busy_max) busy_max = (t5 - stb_t) / 2;
            if (fr != cf) {
                late += fr - cf;
                if (late < 5) fprintf(stderr, "frame %u made, %u expected\n", fr, cf);
            }
            while (cf <= fr) {
                ms_frame(ms, cf, sums);
                cf++;
            }
            compared++;
            int bad = 0;
            for (int k = 0; k < 4; k++) bad |= s40(top->dbg_sums, k) != sums[k];
            // the output: left and right at the volume (32), held at 16 bits
            for (int k = 0; k < 2; k++) {
                int64_t o = (sums[k] * 32) >> 20;
                o = o > 32767 ? 32767 : o < -32768 ? -32768 : o;
                bad |= (int16_t)(top->dbg_out >> (16 * k)) != o;
            }
            if (bad) {
                differ++;
                if (differ <= 10) {
                    printf("frame %u: rtl", fr);
                    for (int k = 0; k < 4; k++) printf(" %lld", (long long)s40(top->dbg_sums, k));
                    printf(" out %d %d", (int16_t)top->dbg_out, (int16_t)(top->dbg_out >> 16));
                    printf(", c");
                    for (int k = 0; k < 4; k++) printf(" %lld", (long long)sums[k]);
                    printf("\n");
                }
            }
        }
    }
    printf("frames compared %ld, different %ld, late %ld, underruns %u, reads: commands %ld, samples %ld, "
        "busiest frame %llu clocks of 2268\n",
        compared, differ, late, top->st_under, cmd_reads, smp_reads, (unsigned long long)busy_max);
    delete top;
    return differ != 0 || late != 0;
}
