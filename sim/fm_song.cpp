// Plays a log of register writes (fakecore's FAKECORE_FMLOG: "<44.1kHz frame>
// <register> <value>" a line) on JTOPL's YM3812, compiled with Verilator,
// and on Nuked OPL3, and writes what each plays at the chip's own 49716Hz:
// <out>_jtopl.raw and <out>_nuked.raw, 16-bit mono. sim/fm_song.sh runs it.
// At most one write is made per two chip samples, on both.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "Vjtopl2.h"
#include "verilated.h"
extern "C" {
#include "../tools/nuked/opl3.h"
}

static Vjtopl2 *top;
static void tick() {
    top->clk = 1; top->eval();
    top->clk = 0; top->eval();
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: fm_song <log> <out prefix> [seconds]\n"); return 1; }
    FILE *f = fopen(argv[1], "r");
    if (!f) return 1;
    struct W { unsigned at, reg, val; };
    std::vector<W> log;
    unsigned a, r, v, first = 0;
    while (fscanf(f, "%u %u %u", &a, &r, &v) == 3) {
        if (log.empty()) first = a;
        log.push_back({ (unsigned)((uint64_t)(a - first) * 49716 / 44100), r, v });
    }
    fclose(f);
    double seconds = argc > 3 ? atof(argv[3]) : 1e9;

    char name[600];
    snprintf(name, sizeof name, "%s_jtopl.raw", argv[2]);
    FILE *fj = fopen(name, "wb");
    snprintf(name, sizeof name, "%s_nuked.raw", argv[2]);
    FILE *fn = fopen(name, "wb");

    static opl3_chip chip;
    OPL3_Reset(&chip, 49716);
    top = new Vjtopl2;
    top->cen = 1; top->cs_n = 0; top->wr_n = 1; top->rst = 1;
    for (int i = 0; i < 400; i++) tick();
    top->rst = 0;
    // the chip takes no writes for a while after its reset
    for (int i = 0; i < 72 * 2000; i++) tick();

    size_t next = 0;
    unsigned n = 0, last = log.empty() ? 0 : log.back().at + 49716;
    int prev = 0;
    while (n < last && n < seconds * 49716) {
        tick();
        if (top->sample && !prev) {
            int16_t s = (int16_t)top->snd, b[2];
            fwrite(&s, 2, 1, fj);
            // the chip needs 72 of its clocks after a write: every second sample
            if ((n & 1) && next < log.size() && log[next].at <= n) {
                OPL3_WriteReg(&chip, log[next].reg, log[next].val);
                top->addr = 0; top->din = log[next].reg; top->wr_n = 0; tick();
                top->wr_n = 1; tick(); tick();
                top->addr = 1; top->din = log[next].val; top->wr_n = 0; tick();
                top->wr_n = 1;
                next++;
            }
            OPL3_Generate(&chip, b);
            fwrite(&b[0], 2, 1, fn);
            n++;
        }
        prev = top->sample;
    }
    fclose(fj); fclose(fn);
    fprintf(stderr, "%u samples, %zu of %zu writes\n", n, next, log.size());
    return 0;
}
