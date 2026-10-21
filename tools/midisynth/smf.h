// Standard MIDI files (format 0 and 1) as one list of events in time, for
// the MIDI synthesizer's tools (render.c, midiplay.c).
#ifndef SMF_H
#define SMF_H

#include <stdint.h>

typedef struct {
    double seconds;      // from the start of the song
    uint32_t tick;
    int track;
    int tempo;           // status 0xFF: a tempo change, microseconds a quarter note
    uint8_t status;      // 0x80..0xEF, 0xF0 for a SysEx, 0xFF for a tempo change
    uint8_t data1, data2;
    int length;          // SysEx: bytes in data (without F0 and F7)
    const uint8_t* data;
} smf_event;

typedef struct {
    smf_event* events;
    int count;
    double length;       // seconds
    int division;        // ticks a quarter note
    int tracks;
    uint32_t end_tick;   // where the last track ends (its end-of-track event)
    uint8_t* file;
} smf_song;

// 0 on failure, with the reason in err
int smf_load(const char* path, smf_song* song, char* err, int errlen);
void smf_free(smf_song* song);

#endif
