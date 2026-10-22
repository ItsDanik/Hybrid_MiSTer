// Internal to the library: the diagnostic report (docs/diagnostics.md),
// between mister_hybrid.c, which counts, and mister_diag.c, which keeps the
// record and writes the report.

#ifndef MISTER_DIAG_H
#define MISTER_DIAG_H

#include <stdint.h>

enum { MH_DIAG_MUSIC_NONE, MH_DIAG_MUSIC_FM, MH_DIAG_MUSIC_SYNTH, MH_DIAG_MUSIC_PI };

typedef struct {
    uint32_t field;     // the core's field counter now
    uint32_t frames;    // frames handed to the core since the last call
    uint32_t never;     // of them, in the field of the one before: that one never shown
    uint32_t twice;     // a field (interlaced: a frame) later than meant: the one before shown twice
    uint32_t late_us;   // the latest a frame came in its field
    uint32_t underruns; // the audio ring ran dry
    uint32_t status2;   // the core's status word 2 now (mode, interlaced, 31kHz, who has the screen)
    uint32_t clock;     // status word 14: the video clock asked for and measured
    uint8_t music;      // MH_DIAG_MUSIC_*
    uint8_t ahead;      // MH_PresentAtField() was used
    uint8_t osd;        // the OSD menu is open
} mh_diag_counts;

// mister_hybrid.c
void mh_diag_take(mh_diag_counts* c); // the counts since the last call
// A copy of the frame on the screen (malloc'd), 0 while the core's own
// picture is there
int mh_diag_frame(uint8_t** pixels, int* width, int* height, int* format, uint32_t palette[256]);
int mh_diag_game_tid(void); // the thread that called MH_Open()

// mister_diag.c
void mh_diag_start(void); // from MH_Open()
void mh_diag_stop(void);  // from MH_Close()

#endif
