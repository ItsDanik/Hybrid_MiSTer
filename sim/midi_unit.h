// sim/midi_unit.cpp and its C side (midi_unit_shim.c)
#include <stdint.h>

typedef struct {
    uint64_t w[16];
    int sec[2];
    uint32_t cnt[2];
    uint64_t x[2];
    int64_t lfo[2];
    int down[2];
    uint32_t blocks;
    int release, killed, first, looped;
    int32_t amp, send[4], fres;
} unit_in;

typedef struct {
    int off, released;
    int sec[2];
    uint32_t cnt[2];
    uint64_t x[2];
    int64_t lfo[2];
    int down[2];
    uint32_t blocks;
    int killed;
    uint64_t incr;
    int32_t amp_step, send_step[4], coef[4];
    int32_t fres, fres_incr;
    int fres_n, silent, q;
} unit_out;

#ifdef __cplusplus
extern "C"
#endif
void unit_check(const unit_in* in, const unit_out* out);
