# MIDI synthesizer in the core: specification

Status: decided, 2026-10-20 (Danik's answers below, "Decisions"). Nothing here is built yet.

## Why

Games of the hybrid ports play General MIDI music (Doom, Heretic, Hexen and Strife through Crispy Doom; Shadow Warrior through jfSW; others to come). Today they only have the core's YM3812 (`rtl/hybrid_fm.sv`), an OPL2, and the AdLib sound. A software synthesizer on the ARM (FluidSynth, Timidity) would cost the games frames: the ARM is a dual-core Cortex-A9 at 800 MHz and a Doom picture already takes 7.3 ms of the 16.8 ms field at 320x240, 12.7 ms at 640x240. So the synthesizer goes where the FM chip is, in the FPGA, split the same way: the ARM does the light work (reading the SoundFont, the MIDI events, which voice plays which sample), the FPGA the heavy work (every sample of every voice, 44,100 times a second).

The result is a wavetable synthesizer that plays SoundFont 2 (`.sf2`) instruments, like FluidSynth, a Sound Blaster AWE32 or a Roland SC-55 module does in its own way. It is part of the framework, so every hybrid core gets it by being built again, and a game uses it through a few calls of `hps/mister_hybrid.h`.

## What there is to build on

- `rtl/hybrid_host.sv`: the one DDR3 port (64-bit, `ddr_*`, 50 MHz `clk`), the shared memory at 0x30000000 (its layout is in the module's header comment), the audio ring at 0x010000 and the FM ring at 0x020000. Read the FM ring's path end to end: it is the model for the MIDI ring.
- `rtl/hybrid_fm.sv`: how a write is made at its audio frame (`frame`, `wr_frame`), and how the chip's sound is added to the audio.
- `hps/mister_hybrid.c` / `.h`: `MH_FMWrite()` and its timing contract ("frame `ahead` of the block the game will queue NEXT"), which the MIDI calls keep. `SHM_SIZE` maps 4 MB now.
- `tools/fakecore.c`: the stand-in for the core on the PC; it has a software FM chip (`tools/nuked`). It gets a software model of the synthesizer the same way (below).
- `sim/`: Verilator test benches; `sim/fm_song.sh` compares JTOPL with Nuked OPL3 on a log of register writes. The synthesizer gets the same kind of comparison against its C model.
- The core's resources today (CrispyDoom, the largest: `core/output_files/CrispyDoom.fit.summary` of that repository): logic 12,294 of 41,910 ALMs (29%), block memory 867,432 of 5,662,720 bits (15%, 138 of 553 M10K blocks), DSP blocks 79 of 112 (71%). So the synthesizer may take up to about 15,000 ALMs, 200 M10K blocks and 16 DSP blocks, and should take far less: one voice circuit used in turn by every voice.

## How it works

```
 game (ARM)                               core (FPGA)
 ----------                               -----------
 MIDI events  --> mh_midi.c:              MIDI ring (DDR3) --> voice registers
   note on/off,     SoundFont presets,                         (block RAM, per voice)
   controllers,     voice allocation,                               |
   program,         generators and          sample memory (DDR3) -> voice engine, time-shared:
   pitch bend       modulators -> voice     per-voice sample cache   fetch, interpolate,
                    parameters                (block RAM)            envelopes, LFOs, filter,
                                                                     pan, gain
                                                                        |
                                                                     mix L/R (+ reverb, chorus)
                                                                        |
                                                  audio ring + FM chip + this = the core's audio
```

### The ARM side (`hps/mister_midi.c`, part of the library)

- **Loads a SoundFont** (`.sf2`): parses it, copies its sample data (16-bit mono PCM) into the sample memory, and keeps its presets, instruments, zones, generators and modulators in ARM memory.
- **Takes MIDI events** from the game, each with its time (the `ahead` of `MH_FMWrite()`): note on, note off, program change, bank select (CC 0 / 32), volume (CC 7), pan (CC 10), expression (CC 11), modulation (CC 1), sustain (CC 64), reverb and chorus sends (CC 91 / 93), all sound off (CC 120), reset all controllers (CC 121), all notes off (CC 123), pitch bend, and the pitch bend range (RPN 0). Channel 10 is the drums (bank 128). General MIDI, GS and XG reset SysEx messages reset the channels.
- **Turns a note into voices**: finds the preset of the channel's bank and program, and in it the instrument zones that match the key and velocity (a note can take several voices, e.g. a layered or stereo sample). For each it works out the SoundFont generators with the default modulators and the zone's own (SoundFont 2.04, sections 8 and 9): sample start, end and loop points, loop mode, root key and tuning, attenuation, pan, the volume envelope (delay, attack, hold, decay, sustain, release, and key number to hold / decay), the modulation envelope and its amounts to pitch and filter, the modulation LFO (delay, frequency, to pitch, filter and volume), the vibrato LFO (delay, frequency, to pitch), the filter cutoff and resonance, the reverb and chorus sends, and the exclusive class (a hi-hat cuts the open hi-hat).
- **Allocates voices**: a free voice, else steals one (released voices first, the quietest, then the oldest of the lowest priority; FluidSynth's rules are a good model). A stolen voice is cut, as FluidSynth cuts it (Danik, 2026-10-20, after listening: the sample-exact match with FluidSynth matters more; the draft had it faded).
- **Sends the voice parameters** to the FPGA through the MIDI ring, as register writes of a voice, each at its audio frame like the FM chip's. Values are converted on the ARM into what the FPGA takes directly: the sample phase increment (from cents, with the ARM's floating point), gains as attenuation in centibels or a linear 16-bit value, envelope rates per sample, LFO increments. A controller change (volume, pan, pitch bend, modulation) sends the changed parameters again for the voices of that channel.
- The ARM does nothing per sample. Its cost is a few hundred register writes a second for busy music, and the SoundFont parse once.

### The ring and the registers

- A ring in the shared memory like the FM ring (0x020000): a new one, the **MIDI ring**, e.g. at 0x030000 (check the layout in `hybrid_host.sv` for room; 64 KB holds 8,192 writes of 64 bits), each entry `{audio frame [31:0], voice [7:0], register [7:0], value [15:0]}`, with "written so far" / "read so far" counters next to the FM ring's in the control and status blocks. Writes are made when their audio frame is played, at once if that is past, in the order written.
- Per voice, registers for: on / off (key on starts the envelopes; key off starts the releases; kill fades fast), the sample (start, end, loop start, loop end as sample numbers in the sample memory; loop mode: none, loop, loop until release), the phase increment (fixed point, e.g. 16.16 samples per output sample, up to several octaves above the root), the gain (attenuation in centibels, or linear), pan (left and right gains), the two envelopes (per stage: a rate or a time in samples, a target level), the two LFOs (increment, delay, depths to pitch, filter and volume), the filter (cutoff, resonance) and the sends. The agent designs the exact map, documents it in the module's header comment as `hybrid_host.sv` documents its memory, and keeps it the same in the RTL, the C model and `mh_midi.c`.
- Global registers: enable, master volume (as `MH_FMVolume()`), reverb and chorus parameters.

### The sample memory

- The SoundFont's samples are in DDR3, in a region the ARM writes and the FPGA reads. It is outside the 4 MB mapped today: the agent finds a region of the 0x30000000 window that nothing else uses (the framebuffers end at 0x3FFFFF; check what Main_MiSTer and the scaler use, e.g. by reading Main_MiSTer's source and `sys/` of the template), maps it in `mister_hybrid.c` only when the MIDI synthesizer is used, and documents it in the layout. Size: enough for GeneralUser GS (about 30 MB); FluidR3_GM is about 140 MB, so the limit is a decision (open questions).
- **Fetching samples is the hard part.** 64 voices at 44.1 kHz read 2.8 million samples a second, and a DDR3 read takes around 100 to 200 ns before its data comes back, so one read per sample cannot keep up. Each voice therefore has a small cache in block RAM (e.g. 16 or 32 samples) that is refilled with bursts of 64-bit words, ahead of where the voice plays, by a fetcher that serves the voice that is closest to running dry. A voice that runs dry plays silence for that sample and is counted (a status counter, for tests); it must not happen with 64 voices, all at the highest pitch the SoundFont asks for, while the video reads the heaviest mode.
- **The video comes first.** The synthesizer's reads share `hybrid_host.sv`'s DDR3 port with the framebuffer scan-out, the palette, the rings and the control block. The scan-out must never be late: the arbiter gives the video priority, and the tests check for a picture that is torn or shifted in 640x480 interlaced with 64 voices playing.

### The voice engine (FPGA, `rtl/hybrid_midi.sv` and modules it uses)

- One circuit, used in turn by every voice: at 50 MHz there are 1,133 clocks per 44.1 kHz frame, so 64 voices get about 16 clocks each, with room left.
- Per voice and output sample: advance the phase by the increment (with the vibrato and modulation LFOs and the modulation envelope applied to the pitch, as cents through an exp2 table), read the samples around the phase from the cache, interpolate (linear at first; 4-point cubic if it fits, it sounds clearly better at high pitches), apply the filter (the SoundFont's resonant 2-pole low-pass; its coefficients from a table by cutoff in cents and resonance, updated at a control rate, e.g. every 64 samples as FluidSynth does), multiply by the volume envelope and the gain, and add to the left and right sums with the pan, and to the reverb and chorus sends.
- Envelopes: the SoundFont's DAHDSR, the attack linear in amplitude, decay and release linear in centibels (exponential in amplitude), as the SoundFont specification says. The ARM gives rates and levels; the FPGA steps them each sample, or at the control rate where that is enough.
- Loops: a voice in a loop wraps from the loop end to the loop start, the interpolation reading across the seam; "loop until release" leaves the loop at key off and plays to the end.
- Mixing: the voices' sums, the reverb and chorus outputs, at a level that leaves headroom (16 loud voices must not clip; the master volume brings it up), held at the limits, then added to the audio as the FM chip's sound is.
- **Reverb and chorus** come after the voices work (phase 2): a Freeverb-style reverb (comb and all-pass filters) and a chorus (a modulated delay), with their delay lines in block RAM (Freeverb needs about 25,000 samples per side: 16-bit, about 0.8 Mbit, which fits). General MIDI music sounds dry without reverb, so it belongs in the first release if it fits.

### The C model

- `tools/midisynth.c` (or a folder of its own): the voice engine in C, sample-exact with the RTL (the same fixed-point arithmetic, the same tables, the same order of operations). It is the specification of the arithmetic, the reference the Verilator test benches compare against, and what `fakecore` runs in place of the FPGA, so that games are developed on the PC as with the FM chip. It reads the same ring and the same sample memory layout as the RTL.
- It is not FluidSynth: musical correctness is checked separately, by rendering the same MIDI file with the same SoundFont in FluidSynth on the PC and comparing them by ear and by measurements (levels, envelopes, pitch). It does not have to match FluidSynth sample for sample.

### The API (`hps/mister_hybrid.h`)

As the FM chip's, with names for the agent to settle; for example:

```c
int  MH_MidiAvailable(void);                       // 0 with a core older than the host version that has it
int  MH_MidiLoadSoundFont(const char *path);       // parse, copy the samples; 0 on failure (the reason in the log)
void MH_MidiEnable(int enabled);                   // needs MH_AudioEnable(1)
void MH_MidiVolume(int volume);                    // as MH_FMVolume()
void MH_MidiEvent(int ahead, int status, int data1, int data2);   // a short MIDI message at its time
void MH_MidiSysEx(int ahead, const uint8_t *data, int length);    // GM / GS / XG reset at least
void MH_MidiReset(void);                           // all voices off, controllers to their defaults
```

- Timing is the audio's, exactly as `MH_FMWrite()`: from the audio callback, the position in the block being filled; from elsewhere, 0.
- The host version goes up by one (it is 14 now): a core built before reports none, `MH_MidiAvailable()` is 0, and a game keeps its OPL music for that case.

## How a game will use it (not part of this work)

Crispy Doom (the CrispyDoom port) already turns Doom's MUS music into MIDI (`mus2mid.c`) and reads MIDI files (`midifile.c`) for its OPL player (`i_oplmusic.c`). It gets a new music module that sends those events to `MH_MidiEvent()` instead, chosen by an OSD option next to *FM Music* (e.g. *Music: FM chip / MIDI*), with the SoundFont from `games/dnk/<Engine>/` or a shared place. jfSW plays MIDI the same way. That is the cores' work, after this. For this work, a test tool plays a MIDI file through the synthesizer on the MiSTer with any core of the new host version, as `tools/fmtest.c` does for the FM chip.

## Tests and when it is done

Each phase ends with its tests passing and Danik's go-ahead for the next.

1. **Design and the C model.** The register map, the fixed-point formats, the memory region and the ring, written down (the module headers and `hybrid/README.md`). `mh_midi.c` with the SoundFont loader and the MIDI handling, the C model, both in `fakecore`. A MIDI file (`tools/midiplay.c` on the PC) renders with GeneralUser GS (or the SoundFont chosen) and sounds right next to FluidSynth's rendering of it: instruments, drums, envelopes, pitch bend, sustain, loops without clicks.
2. **The voice engine in RTL**, without reverb and chorus. Verilator: sample-exact with the C model on a set of register-write logs (single notes of each loop mode, envelopes, LFOs, filter sweeps, 128 voices at once, voice stealing). Fits in the core with timing met (`sta.summary`); the resources it takes go into `hybrid/CLAUDE.md`.
3. **On the MiSTer.** `tools/midiplay.c` plays Doom's music (exported as MIDI) and a dense General MIDI file; no cache underruns (the status counter), no glitch in the picture in any video mode (640x480 interlaced is the heaviest reader), the game's sound effects and the FM chip still right. The game's frame pacing is unchanged: run a CrispyDoom benchmark (`MISTER_HYBRID_PACE=1`) with and without the synthesizer playing.
4. **Reverb and chorus**, if they fit, with the same tests.
5. Documented in `hybrid/README.md` (a section like "The FM chip") and `hybrid/CLAUDE.md`, the host version raised, the template and `new_core.sh` up to date, then committed and pushed as `hybrid/CLAUDE.md` says.

## Decisions (Danik, 2026-10-20)

The six open questions of the draft, answered, and what the interview added.

1. **Voices: 128** (64 at first; raised after the measurements below, Danik, 2026-10-20: with GeneralUser GS 45% of 111 songs stay within 64 voices, 72% within 96, 89% within 128, the rest being release tails, and the DDR3 port carries 128).
2. **Reverb and chorus: both in the first release.** Phase 4 is part of the release, not optional; if one of them does not fit, that is reported with numbers and Danik decides.
3. **The SoundFont: GeneralUser GS is shipped** (its licence read in full first, and nothing committed before), **in one shared folder**, `games/dnk/SoundFonts/` (on a USB drive `usbN/games/dnk/SoundFonts/` too, found as `DATADIR` is). Players add their own `.sf2` files there and pick one **per game in the core's options**:
   - Two lines of the framework's, in every core whose games play MIDI (a core without MIDI leaves both out of its `CONF_STR`): *Music: MIDI / FM chip* (MIDI the default) and *SoundFont: Default, 1 .. 7*. Their status bits are a new framework range, **87..95** (after the HUD margins, 82..86), read by the library; bits 16..23 are not reused, since a rebuilt core's saved settings may still have them set.
   - *Default* is GeneralUser GS; slot N is the Nth `.sf2` of the folder in alphabetical order (the default's own file not counted). When the slot changes, the library shows the file's name on the screen for about 3 seconds, drawn as the FPS counter is (`SoundFont 2: Arachno.sf2`); an empty slot says so and plays the default (`SoundFont 5: none, using GeneralUser GS`).
   - The settings are per game because the status bits are saved per set name (`MH_SetName()`).
   - **GeneralUser GS is the only font shipped** (Danik, 2026-10-20). Others were looked at: the SC-55 Presets that GZDoom and TheForceEngine bundle (deemster, 3.3 MB) and EmperorGrieferus' SC-55 have no licence (the latter is sampled from Roland hardware), Arachno's is "all rights reserved", Timbres of Heaven is for personal use, FluidR3 (MIT) is about 140 MB. A font a player adds is on their own responsibility.
   - **How it is shipped** (Danik, 2026-10-22): in the zip of every port whose games play MIDI (`MIDI_SYNTH` 1), as `games/dnk/SoundFonts/GeneralUser-GS.sf2` with its licence `GeneralUser-GS-LICENSE.txt`. `package.sh` fetches v2.0.3 from the author's GitHub (mrbumpy409/GeneralUser-GS, commit 684543d, which has no releases or tags) and checks both files' SHA-256 (`tools/soundfont.sh`, cached in the core's `build/soundfont/`); it is never committed to a core's repository or this one. The database's repository stores it once, tagged with every port that brings it (`shared.txt`; `hybrid/README.md`, "Distribution"), and refuses a port whose copy differs. The licence (v2.0) allows sharing and modifying it and asks that a site give its own copy rather than link to the author's files, which the database does.
   - **A change takes effect at once:** the music fades out, the new font is loaded in a thread of its own (the game keeps running, its frame pacing unchanged), and the song goes on where it is with the new instruments. One font is in the sample memory at a time.
4. **Sample memory: 64 MB**, at 0x31000000 to 0x34FFFFFF (checked against Main_MiSTer's source and the MiSTer's memory map, below). A font larger than that is refused with a message naming it.
5. **SoundFont 2 only**; no `.sf3`.
6. **Interpolation: 4-point cubic from the start** (table-driven, as FluidSynth's), in the C model and the RTL alike.
7. **Commits:** the work stays uncommitted in `~/Hybrid_MiSTer` until Danik says; one commit per step then, as `CLAUDE.md` says.
8. **Test core:** a scratch copy of the CrispyDoom core (outside `~/MiSTer-CrispyDoom`, the new framework as its `hybrid/`), for the fit, the timing, the tests on the MiSTer (`tools/test_on_mister.sh`, as `CrispyDoom_TEST`) and the frame pacing benchmark; deleted at the end. No core repository changes.

9. **Only in the cores that need it** (2026-10-22, at phase 5): `hybrid_host`'s parameter `MIDI_SYNTH`, 1 in a core whose games play MIDI (the template's `GAME:` line, with the *Music* / *SoundFont* lines), 0 in the others, which then have neither the synthesizer nor its PLL (about 9,500 ALMs, 316 M10K and 20 DSP blocks less, and no 100MHz clock to meet). Status word 1 bit 28 tells the library; without it `MH_MidiWanted()` is the MT32-pi's alone. `rtl/hybrid.sdc` sets the 100MHz clock apart only when it is there. Tested in `sim/tb_host.sv` (`-DNO_SYNTH` with `+trigonly`) and with `FAKECORE_SYNTH=0`.

### After phase 1 (Danik, 2026-10-20)

Danik listened to six songs, ours next to FluidSynth's ("They sound great"), and gave the go-ahead to phase 2.

- **Volume 32 ("as it comes") is FluidSynth's gain 0.5**, its level; the five of the 99 songs that clip do so on the same samples as FluidSynth's.
- **A stolen voice is cut**, as FluidSynth does.
- **A missing or broken SoundFont is said in Main_MiSTer's info box** (the one of "ROMs decrypting"), not on the game's screen: "The OSD lines" and "Where the SoundFonts are" below. The name of a new slot stays on the game's screen.

### With the MT32-pi: host version 16 (Danik, 2026-10-21)

The MT32-pi support (host version 15, `README.md` "MIDI and the MT32-pi") came first, with the same `MH_Midi*()` API, control word 2's MIDI enable and volume, and the info box. The synthesizer was merged onto it as **host version 16**; a core of host version 15 has the Pi only (`status[3] < 16`: no samples mapped, no SoundFont loaded).

- **Both menus stay:** *Music: MIDI / FM chip* (bit 87) and *SoundFont* (88..90) next to the Pi's lines (96..104, shown only while one is connected). With *Music* on MIDI the music goes to the Pi while one is connected and its *MT32-pi* line is On, to the synthesizer otherwise; *FM chip* sends nothing to either (the core's Pi output follows bit 87 too).
- **A switch between them, the game resends the song:** `MH_MidiSynthChanged()` is 1 once, as when the Pi switches synth. The library keeps feeding the synthesizer's channel state (programs, controllers) while the Pi plays, only its note-ons are left out, and stops all its voices when the music leaves it. A new SoundFont in the synthesizer keeps the channels, as before.
- **Info texts:** 1..13 the Pi's synth (the core shows them itself), 14..16 the SoundFont's errors (`MH_INFO_*`), a game's own from 24. The core shows the Pi's at once and the game's once the OSD menu is closed, one driver of hps_io's `info_req`.
- Both are mixed into the audio (`pcm + fm + Pi + synthesizer`, held at the limits); the DDR3 engine reads the synthesizer's samples first, then the FM ring (which carries the Pi's MIDI bytes too).
- Checked after the merge (2026-10-21) in fakecore, D_E1M1 through `midiplay` for 6 s: without a Pi the synthesizer (221 commands, peak 18474); with a Pi (`FAKECORE_MT32=1`) 278 messages to the Pi and the synthesizer silent; the Pi switched off in the OSD mid-song, the music on the synthesizer (peak 18441); *FM chip*, neither; a host version 15 core without a Pi, no MIDI, with one, the Pi.

## Later: a MIDI player and SoundFont manager core (Danik's idea, 2026-10-21)

After this work: a core of its own that plays `.mid` files with a SoundFont of the player's choice (midiplay with a screen and controls; on an MT32-pi too when one is connected) and doubles as a SoundFont manager: each font listed with its size and whether it fits the 64 MB, a preview of a song with each, and the choice of which fonts go into the OSD's slots. That needs the slots chosen by a file rather than by the names' order: e.g. `games/dnk/SoundFonts/slots.txt`, written by the manager, read by the library, the alphabetical order when it is not there. Specified in `docs/midiplayer.md` (the interview, 2026-10-21); `slots.txt` came with host version 16.

## On the MiSTer (phase 3, 2026-10-21)

On the scratch CrispyDoom core, fit 7 (before the merge with the MT32-pi, then numbered host version 15; worst setup slack +0.134 ns), `tools/midiplay.c` on the MiSTer:

- **A bug the MiSTer found:** the parameter slots' free list lost every second slot it took back (`pfl_take` without `!ps_we`), so the core stopped at 519 and 548 commands. Fixed in the RTL; the engine bench and the C model agreed before and after, since the bench had never run long enough to run out.
- **Playing:** D_E1M1 whole, 61 voices at most, 10,319 commands and more, 0 cache underruns; J-cycle whole at 640x480 RGB565, 126 voices, 24,814 commands, 0 underruns. Danik: "It works, everything sounds ok".
- **Against the video:** J-cycle with a moving picture, a frame a field, 40 s in each of 320x200, 640x200, 640x400i, 320x240, 640x240, 8bpp and RGB565, 126 to 127 voices: 0 video lines late (a counter in status word 6, `late_lines`), 0 cache underruns; 640x480i RGB565 for the whole song the same.
- **SoundFont slots** (D_RUNNIN looping): a 70 MB font refused, a broken one refused (FM music instead, the info box says why), VintageDreamsWaves v2 loaded, empty slots 4..7 GeneralUser GS, *Default* back, *Music* FM chip and back. Danik: "Everything works!".
- **USB drive:** no `SoundFonts` folder anywhere, FM music; the SD card's GeneralUser GS loaded in 1.08 s; a folder on `usb0` wins over the SD card's (VintageDreams, 0.08 s); an empty folder on `usb0`, "no SoundFont" and FM music (it is not passed over for the SD card's); GeneralUser GS from `usb0` in 1.07 s.
- **Sound effects and the old library:** the installed CrispyDoom (host version 14) on this core played as before, its FM music on the FPGA chip, the OSD's MIDI lines doing nothing (Danik: "Everything sounded the same"); 10,351 fields in 174 s, 0 lines late. On an older core: "this core has no MIDI synthesizer".
- **After the merge** (host version 16 build, fit 16: worst setup slack +0.326 ns on HDMI, +0.545 ns on the synthesizer's 100MHz; 20,763 ALMs, 369 RAM blocks, 102 DSP): D_E1M1 60 s, 70 voices, 6,619 commands; J-cycle 60 s at 640x480i RGB565, 125 voices, 12,262 commands; 0 underruns and 0 late lines in both; a host version 14 core refuses MIDI; the six slot tests again, the info boxes with their new numbers (Danik: "Everything works. All 6 tests I did, pass.").
- **Frame pacing:** CrispyDoom (its MT32-pi music code, built against the host version 16 library), Plutonia demo1, 12,300 frames after the first 300, `MISTER_HYBRID_PACE=1`, the OSD's *Music* on FM chip and MIDI in turns. Frames never shown plus shown twice: 320x240 FM 0, MIDI 0, FM 0; 640x200 FM 15, 18, 6, MIDI 4, 13, 8, 7, 15. The synthesizer makes no difference. (640x200 is above the 0 that CrispyDoom's notes give for host version 14 with FM music too, so that is not the synthesizer's.)

## Checked against the code and measured (2026-10-20)

- **The DDR3 engine of `hybrid_host.sv` has one request in flight**: it issues a read, takes all its beats, and only then the next (`state`, `S_IDLE`). The video asks for a line one line ahead into a double line buffer (`lbuf`), and line requests come first in `S_IDLE`, so the scan-out has a line of slack (about 63 µs at 15 kHz, 32 µs at 31 kHz).
- **Latency and capacity, measured on the MiSTer** with a probe in a scratch build (reads of 4, 8 or 16 words into the sample region at set rates, lowest priority, as the synthesizer's fetcher would be; the logs are in the session's scratchpad):

  | Load | First word of a read, average / worst | Reads a second at saturation (4 / 8 / 16 words) | Video lines late |
  |---|---|---|---|
  | Doom II demos, 640x240 | 200 ns / 1.1 µs | 3.06 M / 2.45 M / 1.74 M | 0 |
  | 640x480 RGB565 interlaced, a frame every field, the second CPU copying memory nonstop | 245 ns / 1.6 µs | 2.68 M / 2.17 M / 1.59 M | 0 |

  One read in flight is enough. 128 voices in 8-word bursts (32 samples) need 176,000 reads a second at their samples' own rate, 350,000 at twice it, 1.4 million if every voice played at eight times it at once: 16% and 65% of the worst capacity measured.
- **Free DDR3:** Linux has `mem=511M`, and `memmap` reserves 0x1FF00000 up to 1 GB for the FPGA. The scaler uses 0x20000000 to 0x217FFFFF, Main_MiSTer's own framebuffer 0x22000000 to 0x237BBFFF; Main_MiSTer uses addresses from 0x30000000 only for cores that ask for them in their `CONF_STR` (save states, load addresses) and for particular cores (N64, NeoGeo, x86 and others), none of which applies to ours. The sample memory is 0x31000000 to 0x34FFFFFF; 0x30400000 to 0x30FFFFFF stays free for later.
- **Room in the shared memory:** 0x030000 to 0x0FFFFF is unused, so the MIDI ring fits at 0x030000 (128 KB).
- **Resources left in CrispyDoom:** 29,616 ALMs, 415 M10K blocks, 33 DSP blocks.
- **Clocks:** 1,133 clocks of 50 MHz per 44.1 kHz frame, 8.85 per voice for 128 voices: the sample engine is a pipeline that takes a voice every 8 clocks (1,024 clocks for all), with reverb and chorus beside it.
- **GeneralUser GS** (v2.0.3, 30.6 MB of samples): 920 samples, all mono, rates 6 to 48 kHz; loops as short as 10 samples (57 under 32, 128 under 64); filter resonance up to 19 dB in its instruments and 45 dB more in its presets; 2,258 modulators, all of them from velocity, key or a controller (as SoundFont 2 allows), so the ARM works them all out.
- **FluidSynth 2.6.1** (the version installed here, whose renderings are the reference): processes a voice in blocks of 64 frames (envelopes, LFOs, the gain's target and the filter's cutoff once a block, the gain ramped across it); 4-point cubic interpolation (Catmull-Rom, a table of 256 rows); a 2-pole low-pass filter whose Q of 0 dB means no resonance (q = 10^((Q/10 - 3.01)/20)), gain-compensated by 1/sqrt(q); a voice ends when its volume envelope reaches -96 dB. Its default reverb is now Dattorro's; the reference for ours is its Freeverb (`synth.reverb.engine=free`).
- **On the PC:** fluidsynth and timidity are installed; Verilator and Quartus come through the repository's toolchain (`docker.sh`, `sim/run.sh`, the Quartus image), not the PATH.

## Design (phase 0, for Danik's review)

The same for `hps/mister_midi.c`, the C model and the RTL. It replaces what "How it works" above says of the ring (128-byte commands, not 64-bit register writes) and of the voices (128, not 64). Where this says how a value is computed, the C model is the exact definition (bit widths, rounding, the order of operations); the RTL matches it sample for sample.

### Parts

- **ARM** (`hps/mister_midi.c`, in the library): reads the SoundFont, takes the MIDI, works out every generator and modulator of a note's zones, allocates the 128 voices, and sends **commands** through the MIDI ring, each for one voice at one audio frame.
- **FPGA** (`rtl/hybrid_midi.sv` and modules of its own):
  - the *command reader*, part of `hybrid_host.sv`'s DDR3 engine: a command is one burst of 16 words;
  - the *voice memory*: each voice's parameters (from its commands) and state;
  - the *control engine*: once every 64 frames for each voice, the envelopes, the LFOs, and from them the pitch, the filter's coefficients and the gain;
  - the *sample engine*: every frame, every playing voice, one every 8 clocks: interpolation, filter, gain, the four mix sums;
  - the *fetcher*, with a cache of 64 samples for each voice;
  - the *effects* (reverb and chorus, phase 4) and the *mix* into the core's audio.

### What happens in a frame

For audio frame `f` (the frame counter `aud_play`), in this order:

1. Every command whose frame is `f` or earlier, in the order written:
   - `START`: the voice takes the whole parameter block and starts: position at the sample's start, envelopes at their delay, LFOs at 0, filter state 0, gain 0, its block counter 0; and gets a control update at once (step 2's, for this voice, before step 3). Its four send gains stay where its last note left them (0 after a reset), as FluidSynth's voices keep theirs. A `START` to a voice that is playing cuts it, as FluidSynth does with a voice it steals. A voice has two sets of send gains, as each FluidSynth voice has two rvoices: a `START` that cuts a playing voice switches to the other set, and goes on from where that set's last note left it (with one set, the steals in D_IN_CIT and D_HAPPY were at -49 and -48 dB against FluidSynth; with two, -66 and -64). The library takes a voice that has been cut at most once in a block of 64 frames, as FluidSynth (`OVERFLOW_PRIO_CANNOT_KILL`); with none to take, the note is dropped.
   - `UPDATE`: the voice takes the parameters a controller can change (marked *U* below); the next scheduled control update uses them.
   - `RELEASE` (note-off): marked; the next control update after the voice's seventh starts the releases (FluidSynth's 10 ms of a note at least).
   - `KILL`: as `RELEASE`, with the volume envelope's release replaced by the kill rate: FluidSynth's exclusive class, a release of -2000 timecents (the full range in 218 blocks, 316 ms). For the exclusive class.
   - `STOP`: the voice is silent from this frame (*All Sound Off*; the library uses it after fading the music out).
   - `GLOBAL`: the effects' parameters.

   At most 32 commands a frame, of them at most 8 `START`s; the rest wait for the next frame (a chord of more than 8 voices starts one frame, 23 µs, later in its ninth voice).
2. The scheduled control updates: voices `v` with `v mod 64 == f mod 64` (two a frame), if playing and not started in this frame.
3. The sample engine for every playing voice.
4. The effects, the mix, the output.

A command for a frame that is past when it is read is made at once. The game queues its commands at least a field ahead, as `MH_FMWrite()`'s are (the core reads the count of commands written at every vblank), so on time they are sample-exact.

### Shared memory (additions, host version 16)

| Offset | Direction | What |
|---|---|---|
| 0x000010 | HPS → FPGA | control word 2: [31:0] commands written to the MIDI ring so far, [38:32] MIDI volume (32 as it comes, 127 four times), [39] MIDI enable (with audio enable), [47:40] info text, [55:48] info request count (`MH_OSDInfo()`: when the count changes and the text is not 0, the core raises hps_io's `info_req` with `info` = the text, once the OSD menu is closed). The core reads the control block as 3 words. |
| 0x0000D0 | FPGA → HPS | MIDI status, written every vblank: [31:0] commands read, [47:32] cache underruns (counting, wrapping), [55:48] voices playing, [63:56] 0 |
| 0x0000D8, 0x0000E0 | FPGA → HPS | the voices playing, bit *v* of voice *v* (0..63 in the first word), written every vblank |
| 0x0000E8 | FPGA → HPS | [31:0] the audio frame those bits are of. The library frees a voice the engine has ended by them (`mh_midi_voices()`); a released one also when its release must be over. |
| 0x030000 | HPS → FPGA | MIDI ring: 1,024 commands of 128 bytes (to 0x04FFFF) |
| 0x01000000 | HPS → FPGA | sample memory (physical 0x31000000 to 0x34FFFFFF): 64 MB of 16-bit signed samples, little-endian, sample n at byte 2n. The library maps it only while the synthesizer is used. |

### A command (16 words of 64 bits)

w0: [31:0] audio frame; [38:32] voice (0..127); [47:40] command: 1 `START`, 2 `UPDATE`, 3 `RELEASE`, 4 `KILL`, 5 `STOP`, 6 `GLOBAL`; the rest 0. w1..w15: the parameter block (`START`, `UPDATE`) or the effects' parameters (`GLOBAL`, phase 4). The library sends one `UPDATE` a voice and block of 64 frames at most: a second one in the same block replaces the first (or is merged into a `START` of that block).

### The parameter block (w1..w15)

*U*: also taken from an `UPDATE`. "Blocks" are the control engine's 64 frames (1.451 ms). Q*m.n*: fixed point with *n* fraction bits.

| Word | Bits | Field | Format |
|---|---|---|---|
| w1 | [24:0] | sample start | sample number in the sample memory |
| | [49:25] | sample end | the first sample after the last one (as SoundFont `end`) |
| | [51:50] | loop mode | 0 none, 1 loop, 3 loop until release (2 is none) |
| | [61:52] | *U* filter Q | cB (0..960) |
| w2 | [24:0] | loop start | sample number |
| | [49:25] | loop end | the first sample after the loop |
| w3 | [31:0] | *U* pitch | the key's pitch with the tunings and the pitch bend, absolute cents, signed Q.15 rounded down (FluidSynth's `GEN_PITCH` + coarse + fine tune, worked out in double as FluidSynth does: a bend is in 1/1024ths of a cent, and in float and Q.8 a bent note played a cent lower than FluidSynth's for a block now and then, its phase behind from there on; Santa Claus's sax at -42 dB, -80 dB with Q.15) |
| | [47:32] | *U* attenuation | 1/16 cB rounded down, unsigned (initial attenuation, velocity, volume, expression, the modulators) |
| | [63:48] | root | the sample's root key less its pitch correction, cents, signed |
| w4 | [15:0], [31:16] | *U* left, right | gains of the pan, Q0.16 |
| | [47:32], [63:48] | *U* reverb, chorus | send gains, Q0.16 |
| w5 | [21:0] | *U* filter cutoff | absolute cents (1500..13500), Q.8 |
| | [57:22] | rate | the sample's rate over 44,100, Q4.32 rounded (FluidSynth's phase increment is a double, cut to 32.32 at each block: in Q8.24 a long note drifted from FluidSynth's, D_DRONE at -40 dB) |
| w6 | [23:0], [47:24] | *U* mod LFO, vib LFO to pitch | signed cents, Q.8 |
| | [63:48] | mod LFO | delay, blocks |
| w7 | [23:0] | *U* mod envelope to pitch | signed cents, Q.8 |
| | [47:24] | *U* mod LFO to filter | signed cents, Q.8 |
| | [63:48] | vib LFO | delay, blocks |
| w8 | [23:0] | *U* mod envelope to filter | signed cents, Q.8 |
| | [47:24] | *U* mod LFO to volume | signed cB, Q.8 (positive is louder, as in FluidSynth) |
| | [55:48], [63:56] | volume envelope | the low 8 bits of its decay and of its release (below) |
| w9, w10 | | volume envelope | attack, decay (w9); sustain, release (w10): see below |
| w11, w12 | | modulation envelope | the same |
| w13 | [15:0], [31:16] | volume envelope | delay, hold, blocks |
| | [47:32], [63:48] | modulation envelope | delay, hold, blocks |
| w14 | [31:0] | end level | the volume envelope's *x* (Q1.31) below which the voice is inaudible and ends, after its attack, until it has looped: FluidSynth's noise floor (2·10^-7) over the synthesizer's gain and the lowest attenuation the voice can have |
| | [63:32] | end level, looped | the same once the voice has looped, from the loudest sample of the loop (a loop inside the sample's own, in loop mode 1; else the same as the first) |
| w15 | [31:0], [63:32] | mod LFO, vib LFO | increment a block, Q1.31 of the triangle's -1..1: 4 · f · 64 / 44100, f from the frequency in whole cents (FluidSynth's `fluid_ct2hz_real()`), worked out in double and rounded (in Q1.23 an LFO drifted from FluidSynth's, D_BUNNY at -39 dB) |

938 of the 960 bits are used. The depths and the cutoff have fractions of a cent: the mod wheel's vibrato (50 cents · wheel / 127) in whole cents left D_DEAD's guitar at -27 dB against FluidSynth (-57 dB with Q.8). Delays and holds are 12,400 blocks at most (5000 timecents, 18 s).

An envelope's value *x* is 0..1 (the volume envelope's *x* is linear in amplitude in its attack and linear in dB after it: 1 is 0 dB, 0 is -96 dB). FluidSynth's envelopes are in double precision: the volume envelope's *x* is Q1.39 in the engine, the modulation envelope's Q1.31. At Q1.23 a slow release ended 78 blocks before FluidSynth's in 40 s; at Q1.31 with its steps rounded up a drum's release ended a block early, which gave a later note another voice:

| Word | Bits | Field |
|---|---|---|
| first | [31:0] | attack: increment of *x* a block, Q1.31 (2^31: one block), 2^31 / blocks rounded up |
| | [63:32] | decay: decrement of *x* a block (the full range in the decay time; key number to decay applied), bits [39:8] of a Q1.39 |
| second | [31:0] | sustain level *x*, Q1.31 |
| | [63:32] | release: decrement of *x* a block, bits [39:8] of a Q1.39 |

The volume envelope's decay and release are 2^39 / blocks rounded to the nearest, their low 8 bits in w8; the modulation envelope's are 2^31 / blocks rounded up (its low bits are 0). The attack ends at 1, the decay when *x* is strictly below the sustain level, the release when it is below 0 (*x* is then set to that end), as FluidSynth's `fluid_adsr_env_calc()`.

All the SoundFont's conversions (timecents to blocks, cents to Hz to increments, the modulators, the 0.4 factor of attenuation where FluidSynth has it) are done on the ARM in floating point, and only these values reach the FPGA.

### The control engine (a voice, once a block)

1. A pending `RELEASE` / `KILL`: both envelopes go to their release; from the attack, *x* is first converted so that the level goes on without a jump (the volume envelope: from amplitude to dB, a log2 table; the modulation envelope: its convex value), as FluidSynth's `fluid_rvoice_noteoff_LOCAL()`.
2. Both envelopes step one block: delay (count), attack (*x* up by the increment to 1), hold (count), decay (*x* down to the sustain level), sustain, release (*x* down to 0: the voice ends).
3. The LFOs step, once the block counter has passed their delay: a triangle between -1 and 1, Q1.31 (a value past 1 or -1 is folded back by its excess).
4. The modulation envelope's value: in its attack the convex curve of *x* (FluidSynth's table of 128), else *x*; Q1.23.
5. Pitch: cents = pitch + mod LFO · depth + vib LFO · depth + mod envelope · depth (the pitch Q.15, the depths Q.8, the LFOs Q1.31, the envelope Q1.23: the sum Q.39), in whole cents towards 0 (as FluidSynth's `fluid_ct2hz_real()`), less the root; increment = rate · 2^(cents/1200) (a table of 1,200 entries, Q1.32, and a shift), Q8.32 rounded down, held under 256, 2^-32 at least.
6. Filter, as FluidSynth's `fluid_iir_filter_calc()`: cutoff = base + mod LFO · depth + mod envelope · depth, in cents Q.16, held between 5 Hz (-851 cents) and 0.45 · 44.1 kHz (13,494 cents). If it is more than a cent from where the filter is, the filter goes there in steps of the difference / (64 · clip(q, 1, 5)) a frame, over round(64 · clip(q, 1, 5)) frames (a table by Q of both: a resonant filter is swept more slowly, so that it does not click); otherwise it is set at once. The coefficients for the filter's cutoff, at the block's start and again at every frame while it moves (the sample engine's step 3): from tables of sine and cosine (by cutoff in whole cents rounded down, 1500 cents and below the first entry: 12,001 entries, FluidSynth's) and of 1/(2q) and 1/sqrt(q) (by Q, 1 cB steps): alpha = sin/(2q), a0 = 1/(1 + alpha) (a divider), a1 = -2 cos · a0, a2 = (1 - alpha) · a0, b1 = (1 - cos) · a0 / sqrt(q), b02 = b1 / 2, Q2.24. A change of Q takes effect at the next block (FluidSynth ramps q over 64 frames; no General MIDI controller moves it). A block whose gain is 0 and stays 0 (a voice in its delay) is not filtered: the state and the cutoff stay as they are, as FluidSynth skips such a block.
7. Gain target, as FluidSynth's `fluid_rvoice_calc_amp()`: in the attack cb2amp(attenuation) · cb2amp(-mod LFO · depth) · *x*, after it cb2amp(attenuation) · cb2amp(960 · (1 - *x*) - mod LFO · depth); cb2amp(c) = 10^(-c/200) of **whole cB** for c ≥ 0 (FluidSynth's table of 1,441), above 1 for c < 0 (an LFO making the voice louder; 2^(-i/1024) of 1/16 cB and a shift). The sum is in 1/4096 cB before it is cut to whole cB. Q3.26, held under 8.0. The gain goes to the target over the block in 64 equal steps.
8. The four send gains (left, right, reverb, chorus) go from where they are to the parameter block's over the block, in 64 equal steps (Q0.22: exact), as FluidSynth's mixer ramps them (`fluid_rvoice_buffers_mix()`); a voice whose sample ends in the block has them at the target.
9. The voice ends when its volume envelope has finished, or is past its attack and below the end level (w9's, or w12's once it has looped).

The comparison decided the filter: coefficients stepped linearly over a block (an earlier design, without a divider per frame) and direct form I left a Slap Bass's note-off at -43 dB against FluidSynth and D_OPENIN at -38 dB; with the cutoff moved per frame and direct form II they are at -70 and -62 dB. The cost is the coefficient unit at the sample engine's rate (a divider per frame and voice while the cutoff moves) and the wider state.

### The sample engine (a voice, every frame)

1. The four samples around the position (index *i*, fraction *t*): *i*-1, *i*, *i*+1, *i*+2. Past the loop end of a looping voice they are the loop's start again (*i* - loop length); before the loop start of a voice that has looped, its end; before the sample's start, the start; past the sample's end, the last sample (FluidSynth's rules).
2. Cubic interpolation: Σ c_k · s_k, the coefficients from the row *t*[31:24] of a table of 256 (Catmull-Rom, as FluidSynth's), Q1.16.
3. The filter, direct form II as FluidSynth's (the two forms differ while the coefficients move): c = x - (a1 · h1 + a2 · h2) >> 24, y = (b02 · (c + h2) + b1 · h1) >> 24, h2 = h1, h1 = c. The state is 56 bits, held at its limits (the samples are Q.4; a 96 dB resonance at 20 Hz takes 33 bits more than the input, the songs measured 12 at most); y 27 bits, held. No fraction bits are added: measured with 0 to 16 more, the match with FluidSynth is the same. Then, while the cutoff moves: cutoff += its step, the coefficients again (step 6 of the control engine).
4. Gain: out = y · gain; gain += its step.
5. Mixed: left += out · left send, right += out · right send, reverb += out · send, chorus += out · send; each send += its step.
6. Position += increment (index 32 bits, fraction 32: FluidSynth's phase). A looping voice past its loop end goes back by the loop length at once, also after a block's last frame, as FluidSynth's: a voice released there leaves its loop from inside it (wrapped at its next frame instead, a Clavinet with a loop of 26 samples left it a block ahead of FluidSynth's, J-cycle at -44 dB). A voice not looping (or in loop-until-release, released) that is past its end ends at its next frame.

### The fetcher

- Each voice has a cache of 64 samples (16 words), by sample number modulo 64. Ahead of the voice's position, in the order the voice will play them (from the loop end back to the loop start while it loops), the fetcher keeps it full: a burst of 8 words whenever 32 samples are free, for the voice closest to running dry first. A loop that fits in the cache with its neighbours (60 samples or less) is read once and then played from the cache.
- A `START`'s first samples must be there for its frame: the reader takes commands from the ring as soon as they are written (a few ahead) and the fetcher reads a starting voice's first burst before its frame comes.
- A sample that is not there when needed is 0 for that read and counted (status, "cache underruns"). It must stay 0 in every test.
- The fetcher's reads come after the video line's and the audio ring's, before the FM ring's and the control block's.

### The RTL (phase 2)

What `rtl/hybrid_midi.sv` and its modules do, where it differs in detail from the design above (the arithmetic is the C model's throughout).

- **Clocks.** The engine runs on 100MHz of its own PLL (`rtl/pll_midi.v`, from `CLK_50M`), the host's DDR3 engine on its 50MHz. Between them: FIFOs with Gray-coded pointers (commands in, read requests out, free parameter slots back), a toggle for each audio frame (with the frame number and the volume held) and one for the status (held registers). `hybrid.sdc` makes the two clocks unrelated.
- **Commands.** The host reads a command (16 words) as soon as it is written and a parameter slot is free (192 slots of 16 words in a dual-clock RAM), and hands the engine its slot. A `START` takes a cache slot at once and its first samples are asked for then, so they are there before its frame. The engine holds the commands in the order read and takes those due at the start of each frame (C: 32 a frame at most, 8 `START`s), then the control updates (K: about 55 clocks each, `hybrid_midi_ctrl.sv`, one 37×37 multiplier), then the voices (S).
- **The sample engine.** A voice every 8 clocks (128 voices in 1,024 clocks of the 2,268 a frame has), in four stages: the four samples from the cache and the interpolation; the filter (one 27×57 multiplier, used five times) and, while the cutoff moves, the coefficient unit (`hybrid_midi_coef.sv`); the gain; the four sends (a second multiplier). Then the sums held at 40 bits, scaled by the volume, held at 16 bits, published for the next audio frame. The engine makes frame *f* + 1 while the host plays *f*.
- **The cache** (`hybrid_midi_fetch.sv`): 64 samples a voice, by position in the stream the voice plays (not by sample number), so a loop's end and its start are neighbours; position 0 is the sample before the first. Reads of up to 32 samples (9 words) whenever 16 are free, a voice at a time in the order asked. A loop of 52 samples or less is read whole once and then played from the cache. A voice in loop-until-release keeps 16 samples after its loop end (the *tail*), so its release leaves the loop without waiting for a read; a release far from the loop's end re-aims the reads.
- **The host's reads.** `hybrid_host.sv`: the samples after the video line's and the audio ring's reads, before the FM ring's; the commands after the FM ring's, before the control block's. A read of samples the engine needs that has not arrived is an underrun (0 for that read, counted in the status).
- **Size and timing** (a scratch copy of CrispyDoom, Quartus 17.0.2, 2026-10-20): the engine takes 7,728 ALMs, 230 M10K and 21 DSP blocks (control engine 2,154 ALMs and 5 DSP, coefficient unit 255 ALMs and 2 DSP with its sine and cosine tables in 74 M10K, the fetcher 646 ALMs, parameter slots 32 M10K, sample caches 32). CrispyDoom with it: 20,264 ALMs, 368 of 553 M10K, 100 of 112 DSP (alone 12,601, 139 and 81). Timing is met at 100MHz with +0.197ns to spare, the HDMI clock +0.449ns (alone +0.591). It took registers and shorter adder chains: radix-4 dividers, the envelope step and the pitch in more steps, and short formulas for the cache addresses, because Quartus puts the register in front of an M10K's address into the M10K, so the logic before that register must be short. Each change was checked exact against the C model. The margin is thin: a change in the engine's arithmetic needs a fit to see that it still holds.
- **Tests.** `sim/midi_unit.sh <font> <songs>`: the control engine and the coefficient unit, update by update against the C model (`MS_CONTROL_HOOK` in `midisynth.c`). `sim/midi_engine.sh <font> <song> [seconds] [fast]`: the whole engine in Verilator against the C model frame by frame, through a model of the DDR3 (12 to 19 clocks of latency, now and then a video line's read before), at the real 44.1kHz or (`fast`) as fast as the engine goes. `sim/run.sh` (the host's testbench) has a stand-in for the engine (`sim/hybrid_midi_stub.sv`) that checks every word the host hands it.

### The effects (phase 4)

FluidSynth 2.6.1's reverb "free" (Freeverb, `fluid_rev_freeverb.cpp`) and its chorus (`fluid_chorus.c`) with their default settings, as `ms_effects()` of the C model (`tools/midisynth/midisynth.c`) and `rtl/hybrid_midi_fx.sv`, sample-exact with each other. They take a frame's reverb and chorus sums (mono, as FluidSynth's) and add their output to the left and right sums, held at 40 bits, before the volume.

- **The reverb**: room size 0.5, damping 0.2, width 0.8, level 0.7. The input is the reverb sum × 0.03 (Freeverb's fixed gain of twice the mono input); on each side eight combs in parallel (feedback 0.84, a one-pole low-pass in the loop) and four allpasses in series (feedback 0.5); each output the side's and a little of the other's (wet1, wet2). Delay lines of FluidSynth's lengths at 44.1kHz (combs 1,116..1,640 samples, the right side 23 longer; allpasses 225..579).
- **The chorus**: 3 blocks, level 0.6, speed 0.2Hz, depth 4.25ms, sine. One delay line of 2,049 samples; each block reads it at a position a sine LFO moves (the LFOs a third of a turn apart, stepped every 5 samples, the centre moving with them), with FluidSynth's first-order allpass interpolation; blocks 1 and 3 to the left, 2 and 3 to the right (FluidSynth's balance of an odd count), the output wet1/wet2 as the reverb's.
- **Fixed point.** Every delay line in one memory of 27,499 words of 24 bits (the reverb 25,450, the chorus 2,049; 0.66 Mbit). The reverb works in units of the sums >> 6 (its allpass lines in twice that, so that the sum of eight combs fits: The HYBRID Collage takes 0.46 of their range), the chorus's line in units of >> 8. Every product is rounded to the nearest (rounding down left a bias that the combs' feedback made into an error of -28dB). The LFOs' phase is Q.48 of a turn (at Q.32 the rounding of their step moved the read position 0.09 samples off FluidSynth's within three minutes), the sine a table of a quarter wave, 1,025 points of Q.18 (`ms_fxsin`), interpolated.
- **Against FluidSynth**, 99 songs whole (the set of the voice comparison), each effect alone: FluidSynth built with its effects' input and output dumped as doubles (WAV files are too coarse for it: 16 bits and dither), our sends then compared with its and our effects fed its input. The reverb's output differs by -69.8dB in the median and -58.0dB at worst (D_ADRIAN), the chorus's by -80.7dB and -70.3dB (D_MARCH); the sends themselves by -66.8dB and -51.8dB (D_DDTBL2) for the reverb, -72.7dB and -53.5dB for the chorus (the voice engine's rounding, above).
- **The RTL**: on the engine's 100MHz, after the voices of a frame and before its output (the frame waits for it: about 130 clocks of the 2,268). One multiplier (27 × 27) for every product: the 16 combs overlapped, a comb every 3 clocks (the read, b × damp2 and store × damp1, the filter, store × feedback, the write), the allpass lines read in between; the allpasses in series, a clock each; then the chorus's blocks one after another, with every fifth frame the LFO's step. The memory is never cleared: a reverb line reads 0 until it has gone round once since the engine was enabled, the chorus's line where it has not been written yet.
- **Exact** (Verilator, `sim/midi_engine.sh` at the real pace, every frame with the effects and the 16-bit output against the C model, 2026-10-21): D_E1M1 2 s (88,200 frames), Jump! and The HYBRID Collage 20 s each (882,000 frames each, the two loudest of the set): 0 different, 0 late, 0 underruns. The busiest frame 1,132 clocks of 2,268 (the Collage); D_E1M1's went from 482 to 602 with the effects.
- **Size and timing** (the scratch CrispyDoom, Quartus 17.0.2, host version 16 with the MT32-pi): the effects take 1,788 ALMs, 86 M10K and 1 DSP block (estimated: 90 M10K, 4 DSP). CrispyDoom in all: 22,736 of 41,910 ALMs (54%), 455 of 553 M10K (82%), 103 of 112 DSP (92%). Timing met: 100MHz +0.451ns, the HDMI clock +0.090ns (+0.429 before: placement, the effects are not on that clock), hold +0.166ns.
- **On the MiSTer** (2026-10-21): Doom's music and Santa Claus through `tools/midiplay.c` by ear, the reverb clearly heard (Danik), and the same as FluidSynth's in the six songs rendered on the PC. Frame pacing, Plutonia demo1, 12,300 frames a run, frames never shown + shown twice: 640x200 FM 15, 9, 6 and MIDI 13, 8, 3; 320x240 FM 0, MIDI 0 and 0; without the effects (phase 3) 640x200 FM 15, 18, 6 and MIDI 4, 13, 8, 7, 15, 320x240 0: no change.

### Mix and levels

- The four sums of all voices are 40 bits wide. The MIDI volume (32 as it comes) scales left and right, and the result is held at the limits and added to the audio ring's and the FM chip's (the sum held at 16 bits again). "As it comes" is set in phase 1 so that a dense General MIDI song at the volume GeneralUser GS's author recommends for FluidSynth (gain 0.5) does not clip, measured against FluidSynth's rendering.
- Measured (2026-10-20, the 99 songs of the comparison, GeneralUser GS): at 32 = FluidSynth's gain 0.5 the peaks are 15,672 in the median, 27,204 or less in 90% of the songs, and above full scale in 5: Jump! 44,408 (+2.6 dB), Dance 40,972, D_DM2TTL 40,924 (+1.9 dB), Breakout 38,200, the HYBRID Collage 35,636; FluidSynth at 0.5 holds the same samples at the limits as ours (499, 201, 6, 31 and 3 of them). So gain 0.5 does not keep a dense song from clipping. Danik's choice (2026-10-20): 32 stays FluidSynth's 0.5, its level, the five clipping on their loudest chords as FluidSynth's do (not 0.37, -2.6 dB, with which none of the 99 clips). A game's own music volume (Doom's slider, 8 of 15 at the start) scales it before the core does.

### Resources (estimate)

| | Sample engine | Control engine | Fetcher and caches | Tables | Effects (phase 4) | Total | Free in CrispyDoom |
|---|---|---|---|---|---|---|---|
| DSP blocks | 8 (with the coefficient unit) | 1 | 0 | 0 | 4 | about 13 | 33 |
| M10K blocks | 10 (state) | 13 (parameters) | 13 | 70 | 90 | about 200 | 415 |

The filter as FluidSynth's (the control engine's step 6) added a coefficient unit to the sample engine (a divider every 8 clocks, pipelined, in logic; 4 more DSP blocks), the 56-bit state, and the sine and cosine tables in 1-cent steps (12,001 entries each, about 30 M10K blocks each).

### The OSD lines (status bits 87..95)

```
"O[87],Music,MIDI,FM chip;",
"O[90:88],SoundFont,Default,1,2,3,4,5,6,7;",
...
"I,No SoundFont found\nFM music instead\nCopy GeneralUser-GS.sf2 to\ngames/dnk/SoundFonts/,The SoundFont is damaged\nor no SoundFont 2 file\nFM music instead,The SoundFont is too big\n(over 64 MB)\nFM music instead,,,,;",
```

- In a core whose games play MIDI, after the game's own audio lines; a core without MIDI has neither. The `I` line anywhere in the `CONF_STR` (Main_MiSTer takes the first): the texts of Main_MiSTer's info box, `MH_OSDInfo(n)` showing text n (`MH_INFO_*`). Texts 1..7 are the framework's (4..7 empty, kept), a game's own follow from 8. A text has no comma or semicolon; `\n` breaks a line, 30 characters to a line (the box is 32 wide with its frame), shown for 2 seconds. Main_MiSTer reads the request every 100 ms whatever the OSD does (`UIO_INFO_GET`, which clears it), but shows it only while the OSD menu is closed, so the core holds a request until it is (`osd_open`), which is when the player has just picked a slot.
- Bit 87: 0 MIDI (the default), 1 the FM chip. A game plays its OPL music when the OSD says FM chip, the core has no synthesizer (`MH_MidiAvailable()`), or no SoundFont could be loaded (`MH_MidiWanted()` is 0).
- Bits 90..88: the SoundFont. 0 is GeneralUser GS (`GeneralUser-GS.sf2`, its name upstream); N is the Nth other `.sf2` of the folder, by name, case ignored. With a file `slots.txt` in the folder, its lines name the slots instead (`docs/midiplayer.md`, "slots.txt"): a slot it does not list, or whose file is missing, is empty; slot 0 not listed is the rule above. Tested in fakecore (2026-10-21): without the file slots 0, 1, 2 and 5 GeneralUser GS, AWE64, Roland SC-55 and GeneralUser GS; with it each listed slot its file, an unlisted or missing one slot 0's, a CRLF line read. Bits 91..95 are kept for later MIDI options.
- A change of the slot shows `SoundFont 2: Arachno.sf2` for 3 seconds where the FPS counter is, under it when it is on (drawn as `fps_stamp()` draws it, `stamp_text()`); an empty slot `SoundFont 5: none, using` and the default's file name on a second line; slot 0 `SoundFont Default: GeneralUser-GS.sf2`. Nothing is shown for the first font a game loads.
- While the OSD says FM chip the library drops note-ons, and stops every voice when it changes to it, so that nothing of the MIDI sounds while the game turns to its FM music.

### Where the SoundFonts are

- `games/dnk/SoundFonts/` on the first USB drive that has it (`/media/usb0`..`usb7`), else on the SD card; the library finds it itself (`MH_SoundFontDir()`), so the launch scripts do not change.
- Without `GeneralUser-GS.sf2`, *Default* is the first `.sf2` of the folder; with no `.sf2` at all `MH_MidiWanted()` is 0 and Main_MiSTer's info box says so (text 1, `MH_INFO_NO_SOUNDFONT`; Danik, 2026-10-20: the MiSTer's own box, as "ROMs decrypting"). The texts are fixed in the `CONF_STR`, so the box names neither the file nor the drive: `games/dnk/SoundFonts/` stands for the SD card's and a USB drive's.
- A font over 64 MB (text 3), or one that is no SoundFont 2 or damaged (text 2): the box says which, the log has the file and why (`is no SoundFont 2`, `has 140 MB of samples, more than the 64 MB there is room for`). `MH_MidiWanted()` is 0 until another slot is chosen.
- Tested in fakecore (2026-10-20), which prints the text the box would show: a damaged font (the first 3 MB of GeneralUser GS) text 2, after the OSD closed, and nothing while it stays open; an empty folder text 1; a font of 70 MB of samples text 3.
- Fonts without modulators (only the terminal records in `pmod` and `imod`, as TheForceEngine's AWE64 and SC-55 fonts) are fine. Measured with those two against FluidSynth as with GeneralUser GS: D_E1M1, D_RUNNIN and D_ACTION at -78.5 to -80.6 dB, but D_ACTION with AWE64 at -58.7.

### The API (`hps/mister_hybrid.h`, host version 16)

```c
int  MH_MidiAvailable(void);     // MIDI can be played: host version 15 (an MT32-pi) or later (16: the synthesizer)
void MH_MidiEnable(int enabled); // needs MH_AudioEnable(1); loads the slot's SoundFont in a thread
int  MH_MidiWanted(void);        // enabled, the OSD says MIDI, and an MT32-pi is on or a SoundFont is loaded or loading
int  MH_MidiSynthChanged(void);  // 1 once after the music moved between the Pi and the synthesizer (or the Pi lost its settings)
int  MH_MidiReady(void);         // the SoundFont is loaded: notes sound (a game may hold its first song until then)
void MH_MidiVolume(int volume);  // 32 = as it comes, as MH_FMVolume()
void MH_MidiEvent(int ahead, int status, int data1, int data2); // a short MIDI message at its frame
void MH_MidiSysEx(int ahead, const uint8_t* data, int length);  // GM, GS and XG resets, GS drum parts
void MH_MidiReset(int ahead);    // all voices stopped, every channel as at the start
const char* MH_SoundFontDir(void); // games/dnk/SoundFonts on the first USB drive that has it, else the SD card
void MH_OSDInfo(int n);          // text n of the core's I line in Main_MiSTer's info box, for any game
```

- A game: `MH_MidiEnable(1)` after `MH_AudioEnable(1)` when `MH_MidiAvailable()`; at each song `MH_MidiWanted()` chooses between its MIDI and its FM music (or every frame, to follow the OSD); the events from its audio callback with their `ahead`. `tools/midiplay.c` does exactly that with a MIDI file.
- Disabling, or the audio closed, silences the synthesizer and forgets the channels; the font stays loaded until `MH_Close()`. Enabling again starts from a fresh state, the core's read count from 0 (it resets its count when it sees the synthesizer off at a vblank, as with the FM chip).

- `ahead` is `MH_FMWrite()`'s: from the audio callback, the position in the block being filled; from elsewhere 0.
- The library services the synthesizer from `MH_AudioWrite()`, once a block of audio: the voices the core reports ended (status 0xD8..0xEB, read again when the frame changed under it), the OSD's slot, and the block's commands out to the ring. A command that finds the ring full (1,024 waiting) is dropped and counted (`MISTER_HYBRID_STATS=1` prints it); the library holds `midi_lock` for every call into `mister_midi.c`.
- The library watches the slot with the status it reads every field and reloads by itself: the music fades out over 48 ms (the MIDI volume stepped down), every voice is stopped and the library waits until the core has played that frame (nothing reads the sample memory then), the font loads in a thread on CPU1, the channels keep their programs and controllers, notes sound again from their next note-on. A slot whose file is the font already loaded loads nothing. `MH_MidiEvent()` never blocks; while a font loads it drops note-ons and keeps the rest.
- Tested in fakecore (2026-10-20): GeneralUser GS loaded, D_E1M1 through `midiplay` sample for sample as `render.c` plays it until the first note whose voice number differs (the voices are freed by the core's reports, at real vblanks: from there the notes' control updates fall on other frames of their blocks, as they would on the MiSTer); slots 1, 3 (empty), 2, *FM chip* and back, slot 0 in turn while D_RUNNIN played, each change heard and the audio ring never empty.
- The ARM's work: a note-on is a few microseconds (zones, generators, modulators, a command); a controller change sends an `UPDATE` for each voice of that channel. Nothing per frame.

### The C model and the tests

- `tools/midisynth/` (C, no floating point in the engine): frame by frame what this section says, reading the same ring and the same sample memory. fakecore runs it in place of the FPGA (host version 16; the sample memory at 0x01000000 of its file, which is 80 MB and sparse; it takes up to 64 commands ahead of their frames; script lines `music <0|1>` and `soundfont <0..7>` set the OSD's bits); the Verilator benches compare the RTL's output with it, frame by frame, on logs of commands (`FAKECORE_SYNTHLOG=<file>` makes fakecore write one from a running game: a line a command, the frame it was read at and its 16 words in hex).
- Musical checks against FluidSynth 2.6.1 with the same font and file (`fluidsynth -F`, `synth.reverb.engine=free` for phase 4): levels, pitch, envelopes, by ear by Danik.
- `tools/midisynth/render.c` plays a MIDI file through `hps/mister_midi.c` and the model into a WAV file; `tools/midisynth/compare.py` measures it against FluidSynth's. With `MIDISYNTH_FLUID=1` it times as FluidSynth's player does (`fluid_player_callback()`: whole milliseconds at the start of each block, the ticks rounded, track by track) and makes every voice's control update at the blocks FluidSynth has, with no limit to the commands a frame: the two then agree sample for sample but for rounding, so a difference is a bug of one or the other. This mode is for the comparison only; the core's own timing is the frame's.
- What the comparison found, and the engine does as FluidSynth since: the pitch in whole cents; amplitudes in whole cB; the send gains ramped over a block and kept from a voice's last note, two sets of them a voice; a note's zones taken from the last to the first (FluidSynth's loader prepends them), so the voices take the same numbers; the voice envelope's *x* in Q1.39; the end levels before and after a loop; the filter's cutoff moved per frame, direct form II; the LFOs in Q1.31; the phase in 32.32; a loop wrapped at once; the pitch in Q.15 from double.
- **Result** (2026-10-20), GeneralUser GS 2.0.3, 99 songs, each whole: the music of Doom (32), Doom II (35) and Strife (23), and GeneralUser GS's 9 demo MIDIs; FluidSynth 2.6.1 at gain 0.5 without reverb and chorus. The residual (FluidSynth's signal less ours, at the best gain) is 74.4 dB under the signal at best, 66.4 dB in the median, 56.2 dB at worst (D_E3M6); 91 songs under -60 dB, all under -55 dB. The level is the same to 0.01 dB in every song. What is left is rounding: a sum of pitch or filter cutoff that falls within a few thousandths of a cent of a whole cent goes the other way now and then (the depths are Q.8), and the phase or the filter is a little off for the rest of that note.
