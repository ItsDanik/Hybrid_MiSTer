// sim/midi_engine.cpp and its C side (midi_engine_gen.c)
#include <stdint.h>

typedef struct {
    uint32_t sent;      // the frame the library sent it at
    uint64_t w[16];
} gen_cmd_t;

#ifdef __cplusplus
extern "C" {
#endif
long gen_song(const char* font, const char* song, double seconds, uint32_t* frames);
const gen_cmd_t* gen_cmds(void);
const int16_t* gen_samples(void);
#ifdef __cplusplus
}
#endif
