// Stand-in for the FPGA core, to run a hybrid game on the PC.
//
// It keeps the shared memory of hybrid/rtl/hybrid_host.sv in a file: the
// status block and field counter tick at the core's 59.6Hz, the audio ring is
// consumed at 44.1kHz. A script feeds input and takes screenshots. Start it
// first, then the game with MISTER_HYBRID_SHM=<file>.
//
// The FM chip of the core (hybrid_fm.sv) is a software one here, Nuked OPL3
// (tools/nuked/, LGPL): the register writes the game queues are made at their
// audio frames and the chip's sound is in audio.raw with the game's own. The
// MIDI synthesizer is its C model (tools/midisynth/), the sample memory in the
// file too (from 0x01000000, 80 MB in all; the file is sparse).
//
// An MT32-pi (host version 15) is there with FAKECORE_MT32=1, and the OSD's
// MT32-pi line (status bit 96, the script's osd2 line) not off: the game's
// MIDI bytes are then "sent" at their audio frames, and with
// FAKECORE_MIDILOG=<file> written there, "<audio frame> <bytes in hex>" a
// line (tools/midilog2mid.py makes a MIDI file of it, to play on the PC);
// the synthesizer's commands (host version 16) with FAKECORE_SYNTHLOG=<file>.
// Its reports (status bits 24..27) are counted when the script's osd2
// changes the synth, ROM or SoundFont (bits 97..103) or sets bit 104 (Reset
// Hanging Notes), as the Pi's when it has switched: logged "# reports n".
// FAKECORE_SYNTH=0 is a core built without the synthesizer (MIDI_SYNTH 0,
// status word 1 bit 28 clear).
//
// The overlay (host version 17, w0[58]; the game's logo, docs/game_logo.md):
// its handover begins in the first field it is asked for once the intro is
// over (field 300 here, as on the MiSTer, where the counter counts from the
// core's load), status bit 29 from then on. Given up, the core's picture goes out in
// 43 fields before the game has the screen (bit 19); without it the game has
// it at once, nothing fades. Each step is printed with its field. w2[58]
// takes the overlay out as the core does (its fade-in backwards, status bit 30
// once out) until it is cleared; the framebuffers it shows are printed.
// FAKECORE_VERSION=<n> is a core of host version n (17 by default).
//
// usage: fakecore <shm file> [script] [audio.raw]
//   audio.raw receives what the game plays (16-bit stereo, 44.1kHz)
// Script, one command per line (# starts a comment):
//   wait <fields>           let time pass
//   key <index> <0|1>       release/press a key, index = PS/2 set 2 code in hex,
//                           +100 for E0-prefixed codes (5A Enter, 76 Esc, 175 up)
//   joy <hex>               joystick 1 bits (1 right, 2 left, 4 down, 8 up, 10.. buttons)
//   analog <x> <y> [rx ry]  left (and right) stick of joystick 1, -128..127
//   joy2 <hex>, analog2 <x> <y> [rx ry]   the same of joystick 2
//   mouse <dx> <dy> <btn>   move the mouse (y up) and set its buttons
//   osd <hex>               OSD status bits
//   osd2 <hex>              OSD status bits 96..127 (the MT32-pi's lines)
//   osdopen <0|1>           the OSD menu closed / open (Main_MiSTer passes
//                           the core no buttons and keys while it is: the
//                           script's joy and key lines still do)
//   trigger <0..3>          an OSD trigger, T[56]..T[59], selected (the core
//                           flips status bit 56 + n)
//   diag                    Save Diagnostic Report selected (the core flips
//                           status bit 16, host version 16)
//   hud <top> <bottom>      the HUD margins of the CRT Options page, 0..24
//                           pixels (OSD status bits 77..86; 0 at the start)
//   shot <file.ppm>         save the picture the core would show, in the
//                           size of the video mode (not stretched to 4:3)
//   vga31 <0|1>             whether 640x400 and 640x480 are progressive at
//                           31kHz (forced_scandoubler or HDMI Only) or, at
//                           the start, interlaced at 15kHz
//   music <0|1>             the OSD's Music: MIDI / FM chip (status bit 87)
//   soundfont <0..7>        the OSD's SoundFont slot (status bits 88..90)
//   reload                  count the fields from 0 again, as if the core was
//                           loaded again
//   quit                    stop ticking, as if another core was loaded
// Without a script (or after it ends) it runs until killed.

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "nuked/opl3.c"
#include "midisynth/midisynth.c"

#define SHM_SIZE 0x400000
#define SHM_FILE_SIZE 0x05000000 // and the MIDI sample memory, 64 MB at 0x01000000
#define MIDI_QUEUE 64            // commands the reader holds ahead of their frames
#define CTRL_MAGIC 0x4259484D
#define STATUS_MAGIC 0x5359484D
#define FIELD_NS 16768000 // 400 x 262 pixels at 6.25MHz

static volatile uint8_t* shm;
static volatile uint32_t* ctrl;
static volatile uint32_t* status;
static uint32_t field;
static uint32_t palette[256];
static uint32_t palette_seq = 0xffffffff;
static int fb_index, fb_format, fb_mode, ctrl_valid, vga31, msg_shown, osd_open;
static int version = 17;
static int ovl_on, ovl_t, ovl_fade, game_on, game_fb = -1, ovl_shown = -1, ovl_gone, ovl_prints;
static uint32_t audio_fetch;
static uint64_t audio_frac;
static FILE* audio_out;
static opl3_chip fm_chip;
static int fm_on, midi_out, mt32, synth = 1;
static unsigned mt32_reports;
static uint32_t fm_fetch;
static FILE* fm_log;
static FILE* midi_log;
static midisynth* midi_ms;
static int midi_on;
static uint32_t midi_fetch;
static FILE* synth_log;

// one audio frame of the MIDI synthesizer, after taking the commands written
// (as many as its queue holds)
static void midi_frame(uint32_t frame, uint32_t wcount, int volume, int* l, int* r) {
    int64_t sums[4];
    int16_t out[2];
    uint64_t cmd[MH_MIDI_CMD_WORDS];
    int i;

    while (midi_fetch != wcount && ms_queued(midi_ms) < MIDI_QUEUE) {
        const volatile uint32_t* e = (const volatile uint32_t*)(shm + 0x30000 + (midi_fetch % MH_MIDI_RING_CMDS) * 128);

        for (i = 0; i < MH_MIDI_CMD_WORDS; i++) {
            cmd[i] = e[2 * i] | (uint64_t)e[2 * i + 1] << 32;
        }
        if (synth_log) {
            // FAKECORE_SYNTHLOG=<file>: every command, "<audio frame read> <16 words>"
            fprintf(synth_log, "%u", frame);
            for (i = 0; i < MH_MIDI_CMD_WORDS; i++) {
                fprintf(synth_log, " %016llx", (unsigned long long)cmd[i]);
            }
            fprintf(synth_log, "\n");
        }
        ms_push(midi_ms, cmd);
        midi_fetch++;
    }
    ms_frame(midi_ms, frame, sums);
    ms_effects(midi_ms, sums);
    ms_output(sums, volume, out);
    *l += out[0];
    *r += out[1];
}

// the entries of the FM ring that are due at `frame`: register writes for
// the chip, MIDI bytes for the MT32-pi; skipped when that is off
static void ring_frame(uint32_t frame, uint32_t wcount) {
    while (fm_fetch != wcount) {
        volatile uint32_t* e = (volatile uint32_t*)(shm + 0x20000 + (fm_fetch % 4096) * 8);
        uint32_t n = (e[1] >> 24) & 3;
        if ((int32_t)(frame - e[0]) < 0) {
            break;
        }
        if (n != 0 && midi_out && midi_log) {
            uint32_t i;
            fprintf(midi_log, "%u", frame);
            for (i = 0; i < n; i++) {
                fprintf(midi_log, " %02x", (e[1] >> (8 * i)) & 0xff);
            }
            fprintf(midi_log, "\n");
        }
        if (n == 0 && fm_on) {
            OPL3_WriteReg(&fm_chip, (e[1] >> 8) & 0xff, e[1] & 0xff);
            if (fm_log) {
                // FAKECORE_FMLOG=<file>: every write, "<audio frame> <register> <value>"
                fprintf(fm_log, "%u %u %u\n", frame, (e[1] >> 8) & 0xff, e[1] & 0xff);
            }
        }
        fm_fetch++;
    }
}

// one audio frame of the FM chip
static int fm_frame(int volume) {
    int16_t out[2];
    int v;

    OPL3_GenerateResampled(&fm_chip, out);
    v = out[0] * volume / 32;
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

static void tick(void) {
    static struct timespec next;
    uint32_t c0, c1, c2, c3, c4, c5;
    int want;
    int audio_en, fm_en, midi_en;

    if (next.tv_sec == 0) {
        clock_gettime(CLOCK_MONOTONIC, &next);
    }
    next.tv_nsec += FIELD_NS;
    if (next.tv_nsec >= 1000000000) {
        next.tv_nsec -= 1000000000;
        next.tv_sec++;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

    // vblank: latch the control block like the core does
    c0 = ctrl[0];
    c1 = ctrl[1];
    c2 = ctrl[2];
    c3 = ctrl[3];
    c4 = ctrl[4];
    c5 = ctrl[5];
    ctrl_valid = c0 == CTRL_MAGIC;
    // the overlay: its handover, given up the picture going out, then the game
    {
        int want = version >= 17 && ctrl_valid && ((c1 >> 26) & 1);

        int out = want && ((c5 >> 26) & 1);

        if (want && !ovl_on && !game_on && field >= 300) {
            ovl_on = 1;
            ovl_t = 0;
            ovl_gone = 0;
            ovl_shown = -1;
            ovl_prints = 0;
            printf("fakecore: field %u: the overlay's handover begins%s\n", field, ((c5 >> 27) & 1) ? ", rising" : "");
        } else if (ovl_on && want) {
            // as hybrid_host: the fade-in (fields 61..92) backwards when asked
            if (out && ovl_t > 61) {
                ovl_t = (ovl_t > 92 ? 92 : ovl_t) - 1;
            } else if (!(out && ovl_t == 61) && ovl_t < 127) {
                ovl_t++;
            }
            if (out && ovl_t == 61 && !ovl_gone) {
                printf("fakecore: field %u: the overlay is out\n", field);
            } else if (!out && ovl_gone) {
                printf("fakecore: field %u: the overlay comes in again%s\n", field, ((c5 >> 27) & 1) ? ", rising" : "");
            }
            ovl_gone = out && ovl_t == 61;
            // the logo's framebuffer, then the program's first one
            if ((int)(c1 & 3) != ovl_shown && ovl_prints < 2) {
                ovl_prints++;
                printf("fakecore: field %u: the overlay shows framebuffer %d\n", field, (int)(c1 & 3));
            }
            ovl_shown = (int)(c1 & 3);
        } else if (ovl_on && !want) {
            ovl_on = 0;
            ovl_fade = ctrl_valid ? 43 : 0;
            printf("fakecore: field %u: the overlay given up\n", field);
        }
        if (!ctrl_valid) {
            game_on = 0;
            ovl_fade = 0;
            game_fb = -1;
        } else if (!want && !game_on) {
            if (ovl_fade > 0) {
                ovl_fade--;
            } else {
                game_on = 1;
                game_fb = (int)(c1 & 3);
                printf("fakecore: field %u: the game has the screen\n", field);
            }
        } else if (game_on && game_fb >= 0 && (int)(c1 & 3) != game_fb) {
            printf("fakecore: field %u: the game's first frame\n", field);
            game_fb = -1;
        }
        fflush(stdout);
    }
    // a message (0x1800, host version 13): printed when it changes, as the
    // core would show it under its logo
    {
        static uint32_t shown_seq = 0xFFFFFFFF;
        volatile uint32_t* m = (volatile uint32_t*)(shm + 0x1800);
        uint32_t seq = m[0] == 0x534D484D ? m[1] : 0xFFFFFFFF;
        msg_shown = seq != 0xFFFFFFFF;
        if (seq != shown_seq && msg_shown) {
            int l;
            if ((seq >> 17) & 1) {
                printf("fakecore: field %u: list, line %u selected:\n", field, (seq >> 18) & 3);
            } else {
                printf("fakecore: field %u: message%s:\n", field, (seq >> 16) & 1 ? " (error)" : "");
            }
            for (l = 0; l < 4; l++) {
                printf("  |%.71s\n", (const char*)(shm + 0x1808 + l * 80));
            }
            fflush(stdout);
        }
        shown_seq = seq;
    }
    // MH_OSDInfo() (host version 16): a new request in [23:16] shows text
    // [15:8] of the I line in Main_MiSTer's info box, held while the OSD is open
    {
        static uint32_t info_seq;
        static int info_waiting;
        if (ctrl_valid && ((c5 >> 16) & 0xff) != info_seq) {
            info_seq = (c5 >> 16) & 0xff;
            info_waiting = ((c5 >> 8) & 0xff) != 0;
        }
        if (info_waiting && !osd_open) {
            printf("fakecore: info box, text %u\n", (c5 >> 8) & 0xff);
            fflush(stdout);
            info_waiting = 0;
        }
    }
    audio_en = ctrl_valid && ((c1 >> 24) & 1);
    fm_en = audio_en && ((c1 >> 25) & 1);
    midi_en = audio_en && ((c5 >> 7) & 1);
    if (fm_en && !fm_on) {
        OPL3_Reset(&fm_chip, 44100);
    }
    if (!fm_en && !midi_en) {
        fm_fetch = 0;
    }
    fm_on = fm_en;
    // the OSD's Music on MIDI (bit 87) and MT32-pi on (bit 96), or the
    // program's choice (MH_MidiDest(), host version 17)
    want = midi_en && (((c5 >> 24) & 1) ? ((c5 >> 25) & 1) : !(status[34] >> 23 & 1) && !(status[35] & 1));
    if (midi_out && !(want && mt32) && midi_log) {
        // the core tells the Pi to stop all its sound: logged as All Sound
        // Off on every channel
        int ch;
        fprintf(midi_log, "# off\n");
        for (ch = 0; ch < 16; ch++) {
            fprintf(midi_log, "%u %02x 78 00\n", audio_fetch, 0xb0 | ch);
        }
    }
    midi_out = want && mt32;
    // the synthesizer off: every voice silent, the read count from 0
    if (!midi_en && midi_on) {
        ms_reset(midi_ms);
    }
    if (!midi_en) {
        midi_fetch = 0;
    }
    midi_on = midi_en;
    if (!ctrl_valid) {
        // no game: the core's own picture, 640x480
        fb_mode = 4;
    } else {
        fb_index = (c1 & 0xff) > 2 ? 0 : (c1 & 3);
        fb_format = (c1 >> 8) & 1;
        fb_mode = (c1 >> 12) & 15;
        // a mode the core does not have (5 and 6 were 800x600 and 1024x768
        // up to host version 12)
        if (fb_mode > 7 || fb_mode == 5 || fb_mode == 6) {
            fb_mode = 0;
        }
        if (c2 != palette_seq) {
            palette_seq = c2;
            memcpy(palette, (void*)(shm + 0x1000 + ((c1 >> 16) & 1) * 0x400), sizeof(palette));
        }
    }
    if (!audio_en) {
        audio_fetch = 0;
        audio_frac = 0;
    } else {
        // 44100 frames per second, fetched in bursts of 32
        uint32_t target;
        audio_frac += 44100ull * FIELD_NS;
        target = (uint32_t)(audio_frac / 1000000000ull);
        while (audio_fetch + 32 <= target + 128) {
            int16_t block[64];
            int i;
            memcpy(block, (void*)(shm + 0x10000 + (audio_fetch % 16384) * 4), sizeof(block));
            for (i = 0; (fm_on || midi_on) && i < 32; i++) {
                int fm, l, r;
                ring_frame(audio_fetch + i, c3);
                fm = fm_on ? fm_frame((c1 >> 17) & 127) : 0;
                l = block[i * 2] + fm;
                r = block[i * 2 + 1] + fm;
                if (midi_on) {
                    midi_frame(audio_fetch + i, c4, (int)(c5 & 127), &l, &r);
                }
                block[i * 2] = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
                block[i * 2 + 1] = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
            }
            if (audio_out != NULL) {
                fwrite(block, 4, 32, audio_out);
            }
            audio_fetch += 32;
            *(volatile uint32_t*)(shm + 0xC0) = audio_fetch;
        }
    }
    field++;
    // the game has the screen (bit 19): at once, after an overlay once the
    // picture has gone out
    status[2] = fb_index | (fb_format << 8) | (ctrl_valid << 10) | (fb_mode << 12) | (1 << 17)
        | ((vga31 && (fb_mode == 2 || fb_mode == 4)) << 18) | (game_on << 19) | ((msg_shown && !game_on) << 20) | (osd_open << 22)
        | (mt32 << 23) | ((mt32_reports & 15) << 24) | ((unsigned)synth << 28) | (ovl_on << 29) | ((unsigned)ovl_gone << 30);
    status[3] = version;
    status[15] = fm_fetch;
    // the MIDI synthesizer: commands read, voices playing and which, at the
    // frame it will make next
    {
        uint32_t bits[4];
        int n = ms_playing(midi_ms, bits);

        status[36] = midi_fetch;
        status[37] = (uint32_t)n << 16;
        status[38] = bits[0];
        status[39] = bits[1];
        status[40] = bits[2];
        status[41] = bits[3];
        status[42] = audio_fetch;
    }
    status[0] = STATUS_MAGIC;
    status[1] = field;
}

static void screenshot(const char* path) {
    static const int sizes[8][2] = { { 320, 200 }, { 640, 200 }, { 640, 400 }, { 320, 240 },
                                     { 640, 480 }, { 0, 0 }, { 0, 0 }, { 640, 240 } };
    const volatile uint8_t* fb = shm + 0x100000 + fb_index * 0x100000;
    FILE* f = fopen(path, "wb");
    int width = sizes[fb_mode][0];
    int height = sizes[fb_mode][1];
    int i;

    if (f == NULL) {
        perror(path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (i = 0; i < width * height; i++) {
        uint8_t rgb[3] = { 0, 0, 0 };
        if (!ctrl_valid) {
            // the background of the core's picture (WALL_BG of hybrid_host.sv)
            rgb[0] = 0x7F;
            rgb[1] = 0x30;
            rgb[2] = 0xA0;
        } else if (fb_format) {
            uint16_t p = fb[i * 2] | (fb[i * 2 + 1] << 8);
            rgb[0] = (p >> 11) << 3 | (p >> 13);
            rgb[1] = ((p >> 5) & 0x3f) << 2 | ((p >> 9) & 3);
            rgb[2] = (p & 0x1f) << 3 | ((p >> 2) & 7);
        } else {
            uint32_t c = palette[fb[i]];
            rgb[0] = c >> 16;
            rgb[1] = c >> 8;
            rgb[2] = c;
        }
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    FILE* script = NULL;
    char line[256];
    int fd;

    if (argc < 2) {
        fprintf(stderr, "usage: fakecore <shm file> [script] [audio.raw]\n");
        return 1;
    }
    fd = open(argv[1], O_RDWR | O_CREAT, 0644);
    if (fd < 0 || ftruncate(fd, SHM_FILE_SIZE) != 0) {
        perror(argv[1]);
        return 1;
    }
    shm = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    ctrl = (volatile uint32_t*)shm;
    status = (volatile uint32_t*)(shm + 0x40);
    memset((void*)shm, 0, 0x1000);
    {
        const int16_t* samples = mmap(NULL, MH_MIDI_MAX_SAMPLES * 2, PROT_READ, MAP_SHARED, fd, 0x01000000);

        if (samples == MAP_FAILED || (midi_ms = ms_new(samples, MH_MIDI_MAX_SAMPLES)) == NULL) {
            perror("MIDI sample memory");
            return 1;
        }
    }
    if (argc > 2 && (script = fopen(argv[2], "r")) == NULL) {
        perror(argv[2]);
        return 1;
    }
    if (argc > 3) {
        audio_out = fopen(argv[3], "wb");
    }
    if (getenv("FAKECORE_FMLOG")) {
        fm_log = fopen(getenv("FAKECORE_FMLOG"), "w");
    }
    mt32 = getenv("FAKECORE_MT32") != NULL && atoi(getenv("FAKECORE_MT32")) != 0;
    synth = getenv("FAKECORE_SYNTH") == NULL || atoi(getenv("FAKECORE_SYNTH")) != 0;
    if (getenv("FAKECORE_VERSION")) {
        version = atoi(getenv("FAKECORE_VERSION"));
    }
    if (getenv("FAKECORE_MIDILOG")) {
        midi_log = fopen(getenv("FAKECORE_MIDILOG"), "w");
        setvbuf(midi_log, NULL, _IOLBF, 0);
    }
    if (getenv("FAKECORE_SYNTHLOG")) {
        synth_log = fopen(getenv("FAKECORE_SYNTHLOG"), "w");
    }

    while (script != NULL && fgets(line, sizeof(line), script) != NULL) {
        char arg[200];
        int a, b, c = 0, d = 0;
        unsigned long long osd;

        if (sscanf(line, "wait %d", &a) == 1) {
            while (a-- > 0) {
                tick();
            }
        } else if (sscanf(line, "key %x %d", &a, &b) == 2) {
            if (b) {
                status[16 + ((a & 0x1ff) >> 5)] |= 1u << (a & 0x1f);
            } else {
                status[16 + ((a & 0x1ff) >> 5)] &= ~(1u << (a & 0x1f));
            }
        } else if (sscanf(line, "joy2 %x", &a) == 1) {
            status[5] = a;
        } else if (sscanf(line, "analog2 %d %d %d %d", &a, &b, &c, &d) >= 2) {
            status[7] = (a & 0xff) | ((b & 0xff) << 8) | ((c & 0xff) << 16) | ((d & 0xff) << 24);
        } else if (sscanf(line, "joy %x", &a) == 1) {
            status[4] = a;
        } else if (sscanf(line, "analog %d %d %d %d", &a, &b, &c, &d) >= 2) {
            status[6] = (a & 0xff) | ((b & 0xff) << 8) | ((c & 0xff) << 16) | ((d & 0xff) << 24);
        } else if (sscanf(line, "mouse %d %d %d", &a, &b, &c) == 3) {
            status[10] += a;
            status[11] += b;
            status[12] = (status[12] & ~7u) | (c & 7);
        } else if (sscanf(line, "trigger %d", &a) == 1 && a >= 0 && a < 4) {
            status[9] ^= 1u << (24 + a);
        } else if (strncmp(line, "diag", 4) == 0) {
            status[8] ^= 1u << 16;
        } else if (sscanf(line, "hud %d %d", &a, &b) == 2) {
            // OSD status bits 64..95 are status[34]; entry n is n pixels
            // (host version 16)
            status[34] = (status[34] & ~(0x3ffu << 13)) | ((uint32_t)(a & 31) << 13) | ((uint32_t)(b & 31) << 18);
        } else if (sscanf(line, "osd2 %llx", &osd) == 1) {
            // the Pi reports what it plays when it has switched, or after a
            // mute
            if (mt32 && (((uint32_t)osd ^ status[35]) & 0xfe || (osd & 0x100))) {
                mt32_reports++;
                if (midi_log) {
                    fprintf(midi_log, "# reports %u\n", mt32_reports);
                }
            }
            status[35] = (uint32_t)osd & ~0x100u;
        } else if (sscanf(line, "music %d", &a) == 1) {
            // OSD status bit 87 is bit 23 of status[34]
            status[34] = (status[34] & ~(1u << 23)) | ((uint32_t)(a != 0) << 23);
        } else if (sscanf(line, "soundfont %d", &a) == 1) {
            status[34] = (status[34] & ~(7u << 24)) | ((uint32_t)(a & 7) << 24);
        } else if (sscanf(line, "osd %llx", &osd) == 1) {
            status[8] = (uint32_t)osd;
            status[9] = (uint32_t)(osd >> 32);
        } else if (sscanf(line, "shot %199s", arg) == 1) {
            screenshot(arg);
        } else if (sscanf(line, "osdopen %d", &a) == 1) {
            osd_open = a != 0;
        } else if (sscanf(line, "vga31 %d", &a) == 1) {
            vga31 = a != 0;
        } else if (strncmp(line, "reload", 6) == 0) {
            field = 0;
        } else if (strncmp(line, "quit", 4) == 0) {
            break;
        }
    }
    if (script == NULL || feof(script)) {
        for (;;) {
            tick();
        }
    }
    if (audio_out != NULL) {
        fclose(audio_out);
    }
    return 0;
}
