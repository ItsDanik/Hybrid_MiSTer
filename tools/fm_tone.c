// The software chip's side of sim/tb_fm_tone.sv: the same note on Nuked OPL3,
// its samples at the chip's own rate, one a line.
// usage: fm_tone <modulator level, hex> <feedback register, hex> > nuked_tone.txt
#include <stdio.h>
#include <stdlib.h>
#include "nuked/opl3.c"

int main(int argc, char **argv) {
    static opl3_chip chip;
    int level = argc > 1 ? (int)strtol(argv[1], NULL, 16) : 0x08;
    int feedback = argc > 2 ? (int)strtol(argv[2], NULL, 16) : 0x0e;
    int16_t buf[2];
    int i;

    OPL3_Reset(&chip, 49716);
    OPL3_WriteReg(&chip, 0x01, 0x20);
    OPL3_WriteReg(&chip, 0x20, 0x21);
    OPL3_WriteReg(&chip, 0x40, level);
    OPL3_WriteReg(&chip, 0x60, 0xF0);
    OPL3_WriteReg(&chip, 0x80, 0x0F);
    OPL3_WriteReg(&chip, 0xC0, feedback);
    OPL3_WriteReg(&chip, 0x23, 0x21);
    OPL3_WriteReg(&chip, 0x43, 0x00);
    OPL3_WriteReg(&chip, 0x63, 0xF0);
    OPL3_WriteReg(&chip, 0x83, 0x0F);
    OPL3_WriteReg(&chip, 0xA0, 0x98);
    OPL3_WriteReg(&chip, 0xB0, 0x31);
    for (i = 0; i < 8192; i++) {
        OPL3_Generate(&chip, buf);
        printf("%d\n", buf[0]);
    }
    return 0;
}
