// A test of the core's FM chip, for the PC (with fakecore) and the MiSTer:
// plays a scale of eight notes on a plain AdLib instrument, half a second
// each, with silence in the audio ring. What should be heard is exactly that;
// with fakecore the notes are in its audio.raw.
//
//   PC:      gcc -O2 -o fakecore tools/fakecore.c
//            gcc -O2 -Ihps -o fmtest tools/fmtest.c hps/mister_hybrid.c hps/mister_joymap.c hps/mister_ui.c hps/mister_triggers.c -lpthread
//            ./fakecore /tmp/shm script audio.raw &   MISTER_HYBRID_SHM=/tmp/shm ./fmtest
//   MiSTer:  build it with the toolchain, load any hybrid core of host
//            version 10 or later with its game kept from starting
//            (touch /tmp/<name>_nolaunch) and run it.
#include <stdio.h>
#include <string.h>

#include "mister_hybrid.h"

#define BLOCK 512

// the chip's frequency number and block for a note in Hz
static void note(int channel, double hz, int on, int ahead) {
    int block = 4;
    int fnum = (int)(hz * (1 << (20 - block)) / 49716.0 + 0.5);

    MH_FMWrite(0xA0 + channel, fnum & 0xff, ahead);
    MH_FMWrite(0xB0 + channel, (on ? 0x20 : 0) | (block << 2) | (fnum >> 8), ahead);
}

int main(void) {
    static const double scale[8] = { 261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.25 };
    static uint32_t silence[BLOCK];
    int frame = 0, n;

    if (!MH_Open()) {
        fprintf(stderr, "fmtest: no hybrid core\n");
        return 1;
    }
    // a game is one that has shown a frame: a black one
    {
        static uint8_t black[320 * 200];
        static uint32_t palette[256];
        MH_SetFormat(MH_FORMAT_INDEX8);
        MH_SetPalette(palette);
        MH_Present(black, 320);
    }
    MH_AudioEnable(1);
    if (!MH_FMAvailable()) {
        fprintf(stderr, "fmtest: this core has no FM chip (host version 10 is the first with one)\n");
        MH_Close();
        return 1;
    }
    MH_FMEnable(1);
    MH_FMVolume(32);
    // channel 0: a plain two-operator tone that holds while the key is down
    MH_FMWrite(0x20, 0x21, 0); // modulator: sustained, frequency x1
    MH_FMWrite(0x40, 0x18, 0); //            its level
    MH_FMWrite(0x60, 0xF2, 0); //            fast attack, slow decay
    MH_FMWrite(0x80, 0x25, 0); //            sustain level, release
    MH_FMWrite(0x23, 0x21, 0); // carrier
    MH_FMWrite(0x43, 0x00, 0); //            full level
    MH_FMWrite(0x63, 0xF2, 0);
    MH_FMWrite(0x83, 0x27, 0);
    MH_FMWrite(0xC0, 0x06, 0); // feedback 3, modulator into carrier

    // 4.5 seconds: a note every 22050 frames, released 4410 frames before the next
    for (frame = 0; frame < 9 * 22050 && MH_CheckAlive(); frame += BLOCK) {
        for (n = 0; n < 8; n++) {
            int on = 11025 + n * 22050, off = on + 22050 - 4410;
            if (on >= frame && on < frame + BLOCK) {
                note(0, scale[n], 1, on - frame);
            }
            if (off >= frame && off < frame + BLOCK) {
                note(0, scale[n], 0, off - frame);
            }
        }
        MH_AudioWrite(silence, BLOCK, 2048);
    }
    MH_FMEnable(0);
    MH_AudioEnable(0);
    MH_Close();
    printf("fmtest: done\n");
    return 0;
}
