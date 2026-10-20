# MiSTer hybrid core framework

What every hybrid core shares. A hybrid core runs an open source game on the MiSTer's ARM CPU (the HPS) and uses a small FPGA core for what the ARM side has no hardware for: native 15kHz video, audio and the MiSTer's input devices. The two halves talk through shared DDR3 memory.

This repository is the `hybrid/` submodule of every hybrid core repository, so they all use the same code. A game repository adds the game, its `CONF_STR` and its `danik_hybrid_launch.sh`.

What players see calls them **hybrid ports** (READMEs, the launcher's screen, menu entries); inside the framework they stay hybrid cores (`Hybrid_MiSTer`, the `MH_` library, "Hybrid Core Framework" on the core's picture).

Cores built on it: [ECWolf](https://github.com/ItsDanik/ecwolf_MiSTer) (Wolfenstein 3D). [Dethrace](https://github.com/ItsDanik/Dethrace_MiSTer) (Carmageddon) uses the launcher and has its own, older copy of the rest.

| Path | |
|---|---|
| `rtl/` | The FPGA side. `hybrid_host.sv`: scanout from DDR3 in the video mode the game sets (8bpp paletted or RGB565, triple buffered), at 15.6kHz / 59.6Hz wherever the mode allows it, 44.1kHz audio ring, keyboard, mouse, joysticks and OSD status published every vblank. Its header documents the shared memory layout and the video timings. `hybrid_vclk.sv` and `pll_vid` make its video clock, which changes with the mode and the CRT options. `hybrid_fm.sv` is the FM chip (a YM3812, with [JTOPL](https://github.com/jotego/jtopl) in `rtl/jtopl/`): see [The FM chip](#the-fm-chip). A core takes all of it with `hybrid.qip`. `sim/run.sh` runs the testbenches. |
| `hps/` | The ARM side as a small C library (`mister_hybrid.h`): attach to the core, present frames, palette, input, audio, the Menu OK/Back resolution, and the shared screens (game list, error message). |
| `sdl2/` | SDL2 video, audio and input drivers on top of `hps/`, and the script that builds a static SDL2, SDL2_mixer and SDL2_net with them. An SDL2 game needs little more than a recompile. |
| `launcher/distribution.py` | Publishes ports to the database update_all installs them from (`dnk_Distribution_MiSTer`): see [Distribution](#conventions). |
| `launcher/danik_hybrid_ports.sh` | What starts the games on the MiSTer: a daemon that runs `games/dnk/<engine>/danik_hybrid_launch.sh` while the port's core is loaded, and moves ports of the old layout into the new one (see [The launcher](#the-launcher)). `danik_hybrid_cores.sh` is the launcher before it, in its last version (3), which hands a running one over to it |
| `toolchain/` | Docker images: ARM cross compiler matching the MiSTer's glibc, HDL simulation tools. `docker.sh` runs a command in the toolchain. |
| `template/`, `new_core.sh` | The start of a new core repository: FPGA core, build, package and deploy scripts, launch script and READMEs with the name filled in. See [Porting a game](#porting-a-game). |
| `tools/fakecore.c` | Stand-in for the FPGA core on the PC: runs the game against a file instead of the DDR3 window, feeds scripted input, takes screenshots and records audio. Its FM chip is a software one (Nuked OPL3, `tools/nuked/`). |
| `tools/fmtest.c` | Plays a scale on the FM chip, on the PC with `fakecore` or on the MiSTer: the test that the chip is there and in tune. |
| `tools/*.py` | For tests on the MiSTer over ssh: `uinput_kbd.py` and `uinput_pad.py` press keys and gamepad buttons through Main_MiSTer, `status.py` prints what the core publishes. |

## The launcher

`launcher/danik_hybrid_ports.sh` (version 7) is the one thing all hybrid ports share on the SD card. It is developed here and nowhere else; a port takes it from this submodule when its release is packaged. A daemon watches which core is loaded (`/tmp/CORENAME`) and runs that port's `games/dnk/<engine>/danik_hybrid_launch.sh` while it is, with `MISTER_HYBRID_SET` set to what was loaded: a menu entry's set name (`ECWolf_SOD`; the engine is the core's own name in `/tmp/RBFNAME`) or the engine's name. A set name ending in `_TEST` is a build tried with a port's `tools/test_on_mister.sh`, which runs the game itself: the daemon starts nothing for it and, while it is loaded, does none of its work between games either (version 7; the migration and update checks every 10 seconds made the game under test miss frames, CrispyDoom 2026-10-19).

- **Run once from the Scripts menu.** That starts the daemon and puts it into `linux/user-startup.sh`. It also takes over from `danik_hybrid_cores.sh`, the launcher before it: stops it, takes its line out of `user-startup.sh` and deletes it.
- **The launcher before hands over by itself.** A player of the cores released with `danik_hybrid_cores.sh` (version 2) has its daemon running, which installs any newer copy of itself it finds in `games/*/`. Every port therefore brings the last version, 3 (`launcher/danik_hybrid_cores.sh`), as `games/dnk/danik_hybrid_cores.sh`: version 2 installs it within 10 seconds or at the next start of the MiSTer, and as soon as `danik_hybrid_ports.sh` is there (in `Scripts`, or a port's copy, which it puts there) version 3 runs `danik_hybrid_ports.sh takeover` (the first run without the screen) and leaves; run from the Scripts menu it runs the new launcher instead. So `Scripts` never keeps both, and the old ports move into the new layout without the player running anything. Without the new launcher version 3 works as version 2 did. It is not changed again. A player who never ran either launcher gets the core's own message when a port is loaded ("Game not running. Please run danik_hybrid_ports.sh from the Scripts folder.").
- **Ports of the old layout** (`_Other/<engine>_*.rbf`, `games/<engine>/`) still start, and are moved as soon as their new files are there and no game is running: the old `.rbf` deleted, the player's files (data, settings, saved games) moved into `games/dnk/<engine>/` (a file the new port has as well is one of the port's own: the new one stays), the logs into `logs/dnk/<engine>/`, the core's settings and button mappings copied to each of its games' set names (`config/ECWolf_SOD.CFG` from `ECWolf.CFG`, where there is none yet), DXXRebirth's first folders (`games/d1x-rebirth`, `games/d2x-rebirth`) into its own. A folder without our launch script is never touched. A folder the new port does not have goes over whole, one rename however many files it holds (Half-Life's `maps`, Dethrace's `DATA` subfolders); only folders on both sides are merged file by file. One process moves at a time: the screen holds a lock (`/tmp/danik_hybrid_ports.moving`) from before it runs Downloader until it has moved what came, and the daemon moves nothing while the lock is held or Downloader or update_all runs by itself. Version 3 had neither: the daemon started on a port as soon as its new files were there, while Downloader went on, the screen moved the same folder after Downloader, side by side with it, and printed `mv: cannot stat` for every file the other had moved first (found on Danik's MiSTer, 2026-10-17: about 11,000 files, at some 12 a second); nothing was lost. What was moved is written to `logs/dnk/danik_hybrid_ports.log`; `danik_hybrid_ports.sh migrate` does it at once.
- **Choosing the ports.** Run from the Scripts menu (or `danik_hybrid_ports.sh config`), it shows a screen with a tickbox for each port and each of its games, the games the database has (its catalogue, below; none of a port that is not in it), and the launcher on or off. *X: Apply* writes the filter of our drop-in file (`filter = launcher` and, per port, its name when all its games are ticked, else the tags of those that are) and runs Downloader for this database alone (`update.sh --run-only danik_hybrid_ports`), its output on the screen: what is ticked is installed, the rest removed, the player's own files kept. Under it, for each port that was not on the SD card before, the folder the player's own game files go in (`games/dnk/<Engine>/`, whose `README.txt` says which), with the titles of its games that came (version 5), and that the same folders on a USB drive do as well (version 6). Each line of the screen's output box is cut at 48 columns: whatever the launcher prints there stays within them (version 6 found its own new line cut, and an awk of launcher 5 that busybox's awk, the MiSTer's, did not run: test the launcher in `/tmp` on the MiSTer as well as on the PC). Until the player has chosen once (a comment line the screen writes into the file), the ports found on the SD card (either layout) are ticked. That file is the only one it writes; a `[danik_hybrid_ports]` section in `downloader.ini` itself, which Downloader would prefer, is moved into it. The screen is `launcher/ui/danik_hybrid_ports_ui` (C, `launcher/ui/build.sh`), shipped as `Scripts/.danik_hybrid_ports/danik_hybrid_ports_ui`: 320x200 in the core picture's colours and font, scaled by a whole number on larger framebuffers; the pad as keys (A tick, B leave, X apply, L/R a page), a keyboard, and a mouse (the pointer, tickboxes, the scroll bar, the buttons, the wheel). Over SSH or without it, `dialog` does the same.
- **The newest copy wins.** The script has a `VERSION`. The daemon looks at the copies in `games/dnk/*/` when it starts and every 10 seconds while no game runs, installs the one with the highest version over the file in `Scripts` and restarts itself with it.
- **Changing it:** raise `VERSION` with every change that reaches users, keep the last line as it is (the daemon replaces the file while it runs), and keep the contract in the header working for the launch scripts of ports that are already released. `DANIK_HYBRID_ROOT` and `DANIK_HYBRID_TMP` point it at another SD card and `/tmp`, to test it on a copy (it runs on the PC as well).

## Porting a game

**Start from the template**, in a new, empty directory:

```sh
git init
git submodule add -b main https://github.com/ItsDanik/Hybrid_MiSTer.git hybrid
hybrid/new_core.sh <Name> "<Game title>" [<url of our fork of the game>]
```

That gives a repository laid out like the other cores, with a core that builds as it is. What is left for the game is marked `GAME:` in the scripts and `TODO` in the READMEs. A core starts at 320x200; other video modes are features a core adds later (ECWolf: 640x200, VanillaConquer: 640x400). See [Video modes](#video-modes). A change that every core should have is made in `template/` as well as in the core.

**An SDL2 game:** build it against the SDL2 from `sdl2/build.sh` (`CMAKE_PREFIX_PATH=<work>/mister/prefix`). The "mister" drivers are picked when the core is loaded.

- The window is the screen, and its size picks the core's [video mode](#video-modes). Other sizes are cropped or centred in the smallest available mode they fit in. The picture is the same size on the screen (4:3) in all modes: 640x200 has two pixels in the place of each pixel of 320x200, so the game has to scale the two axes on their own. Not a fullscreen-desktop window, which is always 320x200. The display modes SDL lists are the ones available when the game starts.
- Games that draw 8-bit: set the hint `SDL_MISTER_VIDEO_FORMAT=INDEX8`, draw to `SDL_GetWindowSurface()` and set its palette. The FPGA does the palette lookup, so palette fades and flashes cost nothing. Everything else gets RGB565, including the 2D render API (software renderer). The hint is read whenever a window surface is made, so a game can set it before it resizes its window and have one screen in 8-bit and another in RGB565 (KeeperFX: the dungeon and the scaled menus).
- Games that draw all of every frame into the window surface (a software renderer): set the hint `SDL_MISTER_DIRECT=1`. The surface is then the library's own buffer for the next frame (`MH_FrameBuffer()`), the frame goes to the core without `MH_Present()`'s copy (0.3ms of a 320x200 frame on the MiSTer), and the surface's pixels are another buffer after every `SDL_UpdateWindowSurface()`: take them from the surface each frame. They hold the frame before last, so a game that only redraws what changed must not use it. Xash3D does (2026-10-14).
- Audio is converted to 44.1kHz stereo by SDL; the core's sample clock paces the audio thread.
- Keyboard and mouse arrive as SDL events. Joysticks 1 and 2 are SDL joysticks with the d-pad as hat 0, the sticks as axes 0-3, on joystick 1 the analog triggers L2 / R2 as axes 4 and 5 (0..32767; MiSTer does not pass triggers to a core, the library reads them from Linux: `MH_ReadTriggers()`, and a racing wheel's pedals, gas R2 and brake L2, from the sticks Main_MiSTer hands the core for a wheel it knows; while such a wheel is connected joystick 1 is both players' controllers, so that the wheel steers whichever player it is, `MH_WheelConnected()`; SDL's rumble calls on joystick 1 reach the controllers the same way, `MH_Rumble()`) and the buttons of the core's `J1` list as buttons 0.. in that order. Buttons 28 and 29 are Menu OK and Menu Back (see below). While the OSD menu is open the game gets no input at all (host version 14): Main_MiSTer passes the core no buttons and keys then, and the library holds back the rest (sticks, mouse, and what it reads from Linux itself: triggers, stick clicks, the physical Menu OK / Back), and what is still held when the OSD closes until it is let go. `MH_OSDOpen()` says whether it is open, for a game that wants to pause meanwhile.
- `SDL_QUIT` arrives when another core is loaded.
- `#include <mister_hybrid.h>` for the rest: OSD options (`MH_OSDStatus()`), the shared screens (`MH_UI_Menu()`, `MH_UI_Message()`).

**A game without SDL:** use `hps/` directly, as the SDL drivers do (`sdl2/SDL_mistervideo.c` and `SDL_misteraudio.c` are the reference).

**The FPGA core:** `core/` from the template (Template_MiSTer's `sys/`, a 50MHz PLL, `hybrid_host` with its video clock from `rtl/hybrid.qip`, `video_mixer`, `video_freak`). Only the `CONF_STR` in `core/<Name>.sv` is the game's.

**The game's own file:** `template/game/mister_port.c` is a starting point for what the SDL drivers cannot do for the game: its OSD options, frame pacing, the game list, errors on the screen and the exit code.

**Frame pacing:** the core shows 59.64 fields per second and takes the newest frame at every vblank (with an interlaced mode at every second one). A game that renders at another rate, or by the system clock, drops or repeats frames at regular intervals. Show one frame each time the core takes one: wait with `MH_WaitFrame()` (or the `SDL_MISTER_VSYNC` hint) and take the game's time from `MH_FieldCounter()`, not from the system clock. A game with a fixed logic rate that is not the field rate (Wolfenstein 3D: 70 per second) has to draw between two steps of its logic as well, see the ECWolf core. A game whose frames take most of a field (Xash3D: 14ms of 16.8) misses the vblank with every slightly slower one, and the frame before is shown twice while that one is never shown: draw a field ahead instead, starting the frame for field F once the counter is F - 2 and presenting it with `MH_PresentAtField(F)`, which hands it to the core in the field before F (one field more latency, so only where frames are that slow; not for interlaced modes). The template has this behind `DRAW_AHEAD` in `game/mister_port.c` (off). `MISTER_HYBRID_PACE=1` prints how the frames came to the core: the ones never shown and the ones shown twice, which the game's own count of frames per field does not see; `MISTER_HYBRID_AHEAD=0` turns `MH_PresentAtField()` off, to compare (host version 12).

**Testing without a MiSTer:** `gcc -o fakecore tools/fakecore.c`, start `fakecore <file> [script]`, then the PC build of the game with `MISTER_HYBRID_SHM=<file>`. It shows every mode as a plain picture: the two forms of a mode, the CRT options and the analog output are the FPGA's and can only be seen on a MiSTer (`tools/status.py` prints the mode and the video clock the core has measured).

## The FM chip

Since host version 10 the core has a YM3812 (OPL2, the chip of the AdLib and Sound Blaster cards) for games that made their music with one. The game does not work the chip out on the ARM: it sends the register writes it would have sent to the card, and the core adds the chip's sound to the game's audio.

```c
MH_AudioEnable(1);
if (MH_FMAvailable()) {            // 0 with a core older than host version 10
    MH_FMEnable(1);                // chip reset, all registers cleared
    MH_FMWrite(reg, value, ahead); // as often as the music needs
}
```

- **Timing is the audio's.** Every write is made when a certain audio frame is played: frame `ahead` of the block the game will queue next (`MH_AudioWrite()`, or the buffer its SDL audio callback is filling: give the position in it, in 44.1kHz frames). So the music keeps time to the sample whatever the game's frame rate does, and stays in step with the sound effects. A game that makes its music outside the audio path passes 0, "now".
- **A game with a software chip of its own** (DOSBox's, Nuked, MAME's) sends what it would have written into that to `MH_FMWrite()` and no longer runs it. It keeps the software chip for a core without one.
- The chip is mono and goes to both sides. `MH_FMVolume()` sets its level (32 = as it comes, up to 127 = four times); the OSD's *Music Volume* is the game's to apply with it.
- 4096 writes can wait in the core; one more is dropped. No music comes near that.
- An OPL3 is not there: JTOPL's is not finished. Music written for two operators and nine channels, which is all an AdLib has, needs no more.
- On the PC, `fakecore` has a software chip in its place and its `audio.raw` has the music. `tools/fmtest.c` plays a scale; run it on the MiSTer with any core of host version 10 kept from starting its game (`touch /tmp/<name>_nolaunch`).
- A core gets the chip by being built again with this `hybrid/`; nothing in `core/` changes. It costs logic, not the ARM's time.

## MIDI and the MT32-pi

Since host version 15 a game with General MIDI music (Doom's, Shadow Warrior's) can play it on an [MT32-pi](https://github.com/dwhinham/mt32-pi): a MIDI synthesizer on a Raspberry Pi (FluidSynth with SoundFonts, Munt for the Roland MT-32 and CM-32L) that MiSTer players connect to the user port for ao486, with its I2S sound going back to the MiSTer. The core has MiSTer's own module for it (`sys/mt32pi.sv`, as ao486 has it): the game's MIDI messages go to the Pi at 31250 baud, and its sound is mixed into the core's audio.

```c
MH_AudioEnable(1);
MH_MidiEnable(1);                    // once; nothing with a core older than host version 15
if (MH_MidiWanted())                 // an MT32-pi is connected and the OSD has it on
    MH_MidiEvent(ahead, 0x90 | channel, key, velocity);
else
    /* the FM music, as before */;
```

- **The player decides**, in the OSD: four lines of the framework's, shown only while an MT32-pi answers on the user port (menu mask bit 6, `"h6"`), so a player without one never sees them. *MT32-pi* On / Off (bit 96; On by default: the music goes to it), *MT32-pi Synth* SoundFont / MT-32 (bit 97; SoundFont by default, as the games' music is General MIDI), *MT32-pi ROM* MT-32 v1 / v2 / CM-32L (bits 98..99), *MT32-pi SoundFont* **Pi's own** or 0..7 (bits 100..103; Pi's own by default: the Pi keeps the SoundFont it has, which can be any of up to 512, while it takes whatever the core asks for when it first finds it, so the core asks for the one it reported). *Reset Hanging Notes* (bit 104, a trigger, as ao486 has it): the Pi stops all its notes, nothing else changes. The core tells the Pi the synth, ROM and SoundFont over I2C; Main_MiSTer's info box names the synth the Pi reports, while the music goes to it (the template's `"I,"` line). Off, or a game that has MIDI off, and the core tells the Pi to stop all its notes.
- **Unplugged**, the Pi is noticed gone within a second: it polls the core every 50ms, and after a second without a poll the lines go, `MH_MidiWanted()` turns off and the game's music goes back to the FM chip. Plugged in again, it is told to stop its notes and to take the OSD's settings, as a new one. (MiSTer's `mt32pi.sv` alone would keep saying it is there: a tester found that, 2026-10-20.)
- **Timing is the audio's**, as the FM chip's: `MH_MidiEvent(ahead, ...)` and `MH_MidiSysEx()` are sent when audio frame `ahead` of the next block is played. They go through the FM ring (an entry carries up to three bytes of MIDI, `rtl/hybrid_host.sv`), so a game's FM writes and MIDI messages stay in order, and into a queue of 1024 bytes in the core: 31250 baud is 3125 bytes a second, about a millisecond for a note.
- **A game sends the song's settings again after `MH_MidiSynthChanged()`**: the Pi starts every synth, ROM or SoundFont it switches to (and comes out of a mute) with its channels as after a reset, piano on all of them, so a song that set its instruments at its start would play on with the wrong ones. The core counts each report of the Pi (status bits 24..27: it reports when it has loaded what it switched to, after a mute, and after *Reset Hanging Notes*), and `MH_MidiSynthChanged()` is 1 once for each change.
- **A game follows `MH_MidiWanted()`** while it plays: when it turns on, the FM notes are let go and the MIDI channels are told what the song has set so far (program, volume, pan, the other controllers, pitch bend: Crispy Doom's `i_oplmusic.c`, jfSW's `driver_misterfm.c` keep that state for it); when it turns off, `MH_MidiReset()` and the FM music goes on. A General MIDI reset (`F0 7E 7F 09 01 F7`) at the start: the Pi keeps what the core that ran before set.
- `MH_MidiVolume()` is the Pi's level in the mix, 32 as it comes; the game's music volume applies with it. The Pi's sound is mono or stereo as it makes it.
- Not there: the MT32-pi's display drawn over the picture (ao486's *Show Info: LCD*), and MIDI to a USB synthesizer. The game API is the one the planned MIDI synthesizer in the core (`docs/midi_synth.md`) will have, so a game written for the MT32-pi plays on that one too.
- On the PC, `FAKECORE_MT32=1` gives `fakecore` an MT32-pi (its OSD line is the script's `osd2`), and `FAKECORE_MIDILOG=<file>` logs what it is sent, by audio frame; `tools/midilog2mid.py` makes a MIDI file of that, to play with `fluidsynth -F out.wav <font.sf2> out.mid`.
- A core takes the template's lines in `core/<Name>.sv` (no `USER_OUT = '1`; the ports `clk_audio`, `user_in`, `user_out`, `mt32_on`, `info_req`, `info` of `hybrid_host`; menu mask bit 6; hps_io's `info_req` / `info`) and, if its games play MIDI, the four OSD lines after its music option and the `"I,"` line.

## Video modes

| Mode | 15kHz screen | VGA monitor or *HDMI Only* |
|---|---|---|
| 320x200 (the default), 640x200, 320x240, 640x240 | 15.6kHz, 59.6Hz | the same; the core's scandoubler makes it 31kHz for a VGA monitor |
| 640x400, 640x480 | interlaced at 15.6kHz, 29.8 frames per second | progressive at 31.25kHz, 59.5Hz |

800x600 and 1024x768 (host versions 6 to 12, at 31kHz and above only) are gone: few games used them and they made every build of a core longer. `MH_ModeAvailable()` is 0 for them, and a core asked for one shows 320x200 as for any mode it does not have.

A game asks for a mode (`MH_SetMode()`, or the size of its SDL window) and the core picks the form. Which screen is there is the player's to say, with one of three settings:

| Setting | How | 640x400 and above |
|---|---|---|
| 15kHz (default) | nothing set | interlaced at 15kHz |
| VGA | `forced_scandoubler=1` in `MiSTer.ini` | progressive at 31kHz on the analog output and HDMI |
| HDMI only | OSD option *HDMI Only* | progressive on HDMI, the analog output switched off |

- **A 15kHz screen never gets more than 15kHz.** *HDMI Only* does not send the progressive signal to the analog output: it switches that output off (`VGA_DISABLE`) for as long as such a mode is shown. The 200 and 240 line modes still go there, with the OSD over them. With `direct_video=1` the HDMI output itself feeds an analog screen, so the option is hidden and ignored.
- Interlaced, a frame is on the screen for two fields, its even rows in the first and its odd rows in the second, and a new frame is only taken between two frames. On a CRT fine horizontal lines flicker; the HDMI scaler weaves the fields into a steady picture, which is what *HDMI Only* is for: the same modes progressive.
- A change between the forms changes the video clock. The video output is off (no sync) for the few milliseconds that takes, and only comes back when the core has measured the new clock to be right.
- **CRT Options** (not with `forced_scandoubler=1`) adjust the 15kHz forms to the screen without touching the pixels: *Horizontal Size* makes the video clock and the number of pixels in a line 1% smaller or larger per step, so every pixel gets wider or narrower by the same amount and the line stays 64us long, from +7 (where the blanking gets short) down to -24 (a picture four fifths as wide), with the picture staying where it is; *Horizontal Pos* (steps of 2 pixels of 320) and *Vertical Pos* (lines) move the sync pulses. Positive is larger, to the right and down. The size is for the game's picture only: the core's own (the purple screen, its messages and lists) is always shown at size 0, so its text never gets narrow (Danik, 2026-10-16); the positions apply to both. The position stops where the blanking would get too short, which is early for 240 lines, where 22 lines are all there is. The height cannot be changed that way: it is the number of lines, and another number of lines is another refresh rate. None of this changes the HDMI picture.
- Every mode has the same framebuffers, so a game built for an older host works with a newer core and the other way round, with the modes they both know.

## HUD margins

Most games were made for 320x200. A game whose view can be drawn in more lines can also draw it in 240 (320x240, or 640x480), as a mode of its own next to the 200-line ones it keeps (ECWolf: 320x200, 640x200, 320x240 and 640x240). In 240 lines the view fills the whole screen, from the top edge to the bottom one, and the HUD keeps the size it was made at and is drawn over the view, out towards the edges: the top HUD up and the bottom HUD down, by up to the 20 lines there are more. The view goes on behind and around the HUD; nothing is left black or filled. On a CRT the edges are partly hidden (overscan), and every screen hides a different amount, so where the HUD ends up is the player's to set, in the OSD:

```
CRT Options -> Horizontal Size
               Horizontal Pos
               Vertical Pos

               HUD (Top)       0..24, 10 by default
               HUD (Bottom)    0..24, 10 by default
```

- The number is the pixels between the HUD and that edge of a 240-line screen: 0 puts the HUD at the very edge (HDMI, which shows all of the picture), 24 leaves 24 lines. In a 480-line mode it is twice that. The same number puts the HUD in the same place in every game. Above 20 the HUD is further in than in the game's own 200-line layout, over the view.
- In a 200-line mode there is no room to move the HUD: the lines do nothing there, and the game greys them out with `MH_SetMenuMask()` (`D` flags) while one is used.
- The game reads them with `MH_HudMargins(&top, &bottom)` (or `MiSTer_HudRows()` of `template/game/mister_port.c`, which gives the rows for a screen of 240 or 480 lines) every frame and draws its HUD there: the player sees a change at once, while the OSD is still open. Nothing else moves: the view stays 240 lines, and full-screen pictures stay centred at their own size (artwork is only ever scaled by whole numbers, for the 640-wide modes).
- The lines are only in the OSD of a game that has a HUD at that edge: ECWolf has a bottom HUD only, so it has *HUD (Bottom)* and no *HUD (Top)*. A game without a HUD has neither, nor the empty line above them. In a core with several games of which only some have a HUD at an edge, the game hides the line with `MH_SetMenuMask()` (`H0`..`H3` in front of it).
- They are saved with the other options, per game: a game started from its own menu entry (an MGL with a set name) has its own settings file.
- `MH_HUD_DEFAULT` (10) is what an older core without the lines gives. `MISTER_HYBRID_HUD=<top>,<bottom>` in the game's environment fixes them, for a test or a screenshot. In `fakecore`: `hud <top> <bottom>`.
- The option's entries start at 10, its default (the first entry of an option is its default): entry n is (n + 10) mod 25 pixels.

## Several games in one core

A core that runs several games (ECWolf: Wolfenstein 3D, Spear of Destiny and its mission packs; jfSW: Shadow Warrior and its add-ons) has a menu entry for each, an MGL that loads the core with a set name `<core>_<id>` (`ECWolf_SOD`). Main_MiSTer then calls the core by that name: `/tmp/CORENAME`, and the settings and button mappings are saved per game (`config/ECWolf_SOD.CFG`, `config/inputs/ECWolf_SOD_input_*`). The library reads them under that name too (`MH_SetName()`).

The game says which one to start with `MH_PickGame()` (`MiSTer_PickGame()` of `template/game/mister_port.c`), from a table of its games (`mh_game`: the set name's id, the name in the list, the files that make the game, and the message when they are missing):

- **From a game's menu entry:** that game. A shareware or demo version has no menu entry: the full game's entry starts it when the full game is not there. When neither is, its message says which files to copy where.
- **From the core itself** (Recent, Zaparoo, a bootcore): of the games whose files are there, the only one, or a list to pick from; a shareware has a line of its own, also next to its full game. None: a message.

The list (`MH_UI_Menu()`, host version 13) is on the core's picture, in the place of a message: 4 games at a time, the selected one on a pink bar, a dimmed `^` / `v` in the last column where there are more; the dots stand still. Up/Down, A or Enter to start, B or Esc to leave; the picked game's "Loading ..." message takes its place (`MiSTer_PickGame()`). Names are cut at 69 characters.

A port that finds its games by itself (jfSW knows its GRP files by their checksum, whatever their name) gives the games it found `files` NULL (always there) and the others `MH_NOT_THERE` (never there), with their message. Files are found in any case and with `*` and `?` (`*.SOD`, `addons/WT.GRP`). `%s` in a message is the game's folder as the player sees it (`games/dnk/ECWolf`, `MH_GamePath()`): messages never name a folder of their own.

## Messages

Since host version 13 a message is shown on the core's own picture (the purple screen), under the logo, in the badge's font: what is missing, what went wrong, what is loading. The core shows its picture with the message whenever a message is up and no game has the screen.

**A game whose files are missing says which files go where** (rule, `CLAUDE.md`): the files by name or pattern, the game they are for, the folder (`%s`), in a message that stays until the player leaves the core. Never a bare "not found" or a message in the log only.

**The space is fixed: 4 lines of 71 characters** (`MH_MSG_ROWS`, `MH_MSG_COLS`). Write every message to fit: what is wrong and what to do, the files and where they go (`games/dnk/<Engine>/`, or `usb0/games/dnk/<Engine>/` when the game runs from a USB drive: always `%s`, `MH_GamePath()`, never written out, and nothing after it that says which drive: no "on the SD card"), in plain words. Characters 32..126 only. The text is wrapped at spaces and at `\n`; empty lines are left out, there is no room for them; a longer text is cut, its fourth line ending in "...", and all of it is in the log. To try one: `tools/make_wall.py --preview out.png --message "line|line|line|line"`, or on the simulated core `MESSAGE="line|line" sim/wall_shot.sh out.png`.

| From | How | |
|---|---|---|
| the game | `MH_UI_Message(title, text, flags)` (`MiSTer_ShowError()` of `template/game/mister_port.c`) | with `MH_UI_ERROR` the dots under the logo stand still; with `MH_UI_WAIT` the call returns only once the player has left the core, so the message stays whatever the launcher does. Without them ("Loading...") the message is cleared when the game takes the screen again with its next frame. A game that has the screen gives it back for the message. The title only goes to the log. |
| the game, directly | `MH_ShowMessage(text, MH_MSG_ERROR)`, `MH_ClearMessage()`, `MH_MessageShown()` | |
| the launch script | `./mh_message [--error] "<text>"`, `--clear`, `--shown` (`tools/mh_message.c`, built by `build.sh`, shipped next to the game) | after the game has left: a game that crashed or left with an error code and put no message up gets "The game stopped unexpectedly (exit code N). Its log: ...", and the core stays loaded while a message is up instead of going back to the menu |
| the core itself | no game in 12 seconds after it was loaded (`WALL_TIMEOUT` of `hybrid_host.sv`) | "Game not running. Please run danik_hybrid_ports.sh from the Scripts folder.": the launcher is not running |

A message is a block in the shared memory at 0x001800 (`hybrid_host.sv`). The core picture comes back at once for it, without the 3 seconds of black after a game has left and without the intro.

## Conventions

These make the cores look and work alike. Where the framework can enforce one, it does.

**Naming and layout on the SD card**

Two names: **`dnk`** for what is on the SD card (the `dnk-` of menu entries and cores, the `dnk/` folders) and the distribution repository; **`danik`** for the script and the database (`danik_hybrid_ports`). Players find a game by its own name, all our ports together in the `Other` menu, and never one that clashes with another developer's port of the same game.

| What | Where |
|---|---|
| a game's menu entry | `_Other/dnk-<Game title>.mgl`, one for each game, also for a core with one |
| the core | `_Other/dnk/dnk-<Engine>_YYYYMMDD.rbf` (`_Other/dnk/` has no `_` in front, so the menu does not show it: only the entries load it) |
| the game, its data, settings and saved games | `games/dnk/<Engine>/`, the binary `games/dnk/<Engine>/<Engine>`; the data, settings and saved games may be in `games/dnk/<Engine>/` on a USB drive instead (below) |
| logs | `logs/dnk/<Engine>/` |
| the core's name (`CONF_STR`) | `<Engine>` |
| settings and button mappings | a core with one game: `config/<Engine>.CFG`, `config/inputs/<Engine>_input_*`; one with several: per game, `<Engine>_<Game>.CFG`, `<Engine>_<Game>_input_*` |
| the launcher | `Scripts/danik_hybrid_ports.sh`, its screen `Scripts/.danik_hybrid_ports/danik_hybrid_ports_ui` |

- **The engine** is the source port's name (ECWolf, jfSW, YQuake2, Xash3D), never a game's alone: another developer's port of a game has a core of that game's name (on Danik's MiSTer: a Quake core named `MiSTer_Quake`).
- **A game's menu entry** is an MGL that loads `_Other/dnk/dnk-<Engine>` (Main_MiSTer takes the newest dated `.rbf`). With several games each has a set name, `<setname same_dir="1"><Engine>_<Game></setname>`: short, no spaces, 31 characters at most (Main keeps a core's name in 32 bytes). Main then calls the core by it (`/tmp/CORENAME`; `/tmp/RBFNAME` is the engine) and saves its settings and button mappings under it. A core with one game has no set name. `package/games.txt` of a core lists its games (`<Game>|<title>`); `package.sh` makes the entries, without what a file name cannot have (`dnk-SoD Return to Danger.mgl`). A shareware or demo version has no entry of its own: [Several games in one core](#several-games-in-one-core).
- `games/dnk/<Engine>/danik_hybrid_launch.sh` is what `danik_hybrid_ports` runs, with the set name in `MISTER_HYBRID_SET`. Game data goes into `games/dnk/<Engine>/`, unless a core has a reason for its own place; everything the game writes (settings, saves) stays there; the log goes to `logs/dnk/<Engine>/` when the game has left: while it runs, what it prints is kept in `/tmp`, because the SD card is mounted `sync` and every line printed to it stops the game for about 9ms. Messages name the folder from where the game runs (`MH_GamePath()`), never one of their own.
- **Game data on a USB drive** (host version 14): the launch script runs the game in the first of `/media/usb0`..`usb7/games/dnk/<Engine>/` that exists, else in `games/dnk/<Engine>/` on the SD card (`DATADIR`; Main_MiSTer looks for a core's games on the USB drives before the SD card in the same way). The folder on a USB drive wins whenever it is there, also with files missing: the game's message then names it (`MH_GamePath()`: `usb0/games/dnk/<Engine>`). What the port brings stays on the SD card, where Downloader installs it (`GAMEDIR`): the programs and the libraries the game loads, since USB drives are mounted `noexec`, and `mh_message`, `games.txt` and the READMEs. So the game finds its data, settings and saved games in the current folder (or a folder its command line names, from `$DATADIR`) and what came with the port in `$GAMEDIR`; the launch script's `GAME:` line says which for each port. `TEST_USB=1 tools/test_on_mister.sh` tries it.
- Game data is never shipped, shareware and demos included. The README names the files to copy and where, as whole folders: "everything in the game's folder", or "the `DATA` folder with all its files", never files that are not used and can be left out. A file the port does not read does no harm on the SD card; one taken out by mistake leaves a player with a game that half works (Screamer 2's menus stayed black without one `ANIMS` file, after its README had said which could go: 2026-10-18). The game's check at its start looks for every file it opens, so that a missing one is named on the core's picture.
- Release: `<Engine>_YYYYMMDD.zip` in the repository's `releases/` folder, committed and pushed; no GitHub release (rule, `CLAUDE.md`). The zip is to extract at the root of the SD card, in the layout above, with `README.txt` and the licenses in `games/dnk/<Engine>/` and the launcher and its screen in `Scripts/` (and a copy of both in `games/dnk/<Engine>/`, see [The launcher](#the-launcher)). The README lists running `danik_hybrid_ports` once from the Scripts menu as a requirement.

**Distribution**

- The database `danik_hybrid_ports`, from [dnk_Distribution_MiSTer](https://github.com/ItsDanik/dnk_Distribution_MiSTer) (below), comes as a drop-in file of Downloader (2.4 and later): `/media/fat/downloader_danik_hybrid_ports.ini` (`launcher/downloader_danik_hybrid_ports.ini`, also in every release zip). Players copy it to the SD card; nobody edits `downloader.ini`. A database cannot install such a file itself (Downloader refuses), so it never comes through update_all.

  ```ini
  [danik_hybrid_ports]
  db_url = https://raw.githubusercontent.com/ItsDanik/dnk_Distribution_MiSTer/db/db.json.zip
  filter = launcher
  ```

- **Opt-in.** The file as shipped has `filter = launcher` (and the database's default filter is the same): update_all installs the launcher alone; the games are chosen on the launcher's screen, which writes `filter = launcher <tags>` into that file and runs Downloader for this database (`update.sh --run-only danik_hybrid_ports`). A filter of its own also keeps a global `[mister]` filter from being used for this database. Leaving: untick everything and apply (the ports go), then delete the file.
- **Tags.** The launcher and its screen: `launcher`. An engine's files (its `.rbf`, `games/dnk/<Engine>/`): `<engine>` and every one of its games' tags. A game's menu entry: `<engine>_<game>` and `<engine>`. All lower case; Downloader ignores `-` and `_` in them, so they have to differ without them. A port whose games are all wanted is `<engine>` in the filter, otherwise the tags of its games.
- **Taking one out** removes what Downloader installed for it (its entries, its `.rbf`, our files in `games/dnk/<Engine>/`); the player's data, saves and `config/` stay, and ticking it again brings it back as it was.
- **The catalogue**, `Scripts/.danik_hybrid_ports/catalogue.txt` (tag `launcher`), lists every engine and game the screen offers, a line each, `<Engine>|<Game>|<title>`. `distribution.py` makes it from the `games.txt` of every port in the database (each core's `package/games.txt`, which `package.sh` puts in the zip as `games/dnk/<Engine>/games.txt`), so the screen offers exactly what the database has: never a port that is not published, and a new port or game needs no new launcher. Release zips never carry it. Without it (a launcher from a zip, before update_all has run), the screen offers the ports on the SD card: their `games.txt`, or one line for a port of the old layout.
- **Publishing** is its own step, after a release and only when Danik asks for it ("publish <port>"): a release (the zip in the core's `releases/`) puts nothing in the database, so a port can be released and still unannounced. [dnk_Distribution_MiSTer](https://github.com/ItsDanik/dnk_Distribution_MiSTer) holds `ports.txt` (the published ports, `<Engine>|<zip>`), `files/` (what the database installs, as on the SD card: each published zip extracted, the launcher, its screen and the catalogue), `README.md` (for players, with a table of the published ports: their games, the folder their game files go in and a link to the port's `README.txt`, made again with every publish and remove) and, on its branch `db`, `db.json.zip`, whose files are served from the commit of `main` they are in. It is made only with `launcher/distribution.py` from a checkout of this repository (a clone of the distribution repository next to it, `~/dnk_Distribution_MiSTer`):

  ```sh
  hybrid/launcher/distribution.py publish ~/dnk_Distribution_MiSTer releases/<Engine>_YYYYMMDD.zip  # a port in, or its new release
  hybrid/launcher/distribution.py publish ~/dnk_Distribution_MiSTer    # the launcher alone, after a change to it
  hybrid/launcher/distribution.py remove ~/dnk_Distribution_MiSTer <Engine>
  hybrid/launcher/distribution.py push ~/dnk_Distribution_MiSTer       # to GitHub: only now do players get it
  ```

  `publish` and `remove` commit locally on `main` and make `db` again; `push` sends both. `publish` takes out what the port had before and extracts the zip without its `Scripts/`; the database's launcher and screen are always this repository's (`launcher/danik_hybrid_ports.sh`, the screen built with `launcher/ui/build.sh`, which needs the toolchain), so a launcher change reaches players with `publish` alone, no port packaged again. It refuses a zip without `games.txt`, one whose launcher copy (`games/dnk/<Engine>/danik_hybrid_ports.sh`) has this launcher's `VERSION` but other contents (packaged before a change that kept the `VERSION`) or a newer one, a menu entry of another core, and a tree in which a file belongs to no published port, a game has no menu entry, or two tags are the same without `-` and `_`. `distribution.py check` runs those checks alone. The tags are written as Downloader compares them (lower case, without `-` and `_`); the database's default filter is `launcher`.
- **Testing the database** before `push`: `distribution.py db <checkout> http://dnk.invalid/files/ <dir>/db.json.zip` writes it with other URLs; Downloader (its source, `python3 <Downloader_MiSTer>/src --run-only danik_hybrid_ports` with `PC_LAUNCHER=<card>/downloader_pc_launcher.py`) then installs from a local server on a folder that stands for the SD card, through `HTTP_PROXY=http://127.0.0.1:<port>` (Downloader refuses URLs of local addresses; the server has to take the proxy's full URLs as paths). The drop-in file's `db_url` is `http://dnk.invalid/db.json.zip` for it; the launcher writes its own `db_url` back with every Apply.
- The launcher before (`danik_hybrid_cores.sh`) is in the database as `games/dnk/danik_hybrid_cores.sh` (tag `launcher`, always this repository's), for the handover above. Its own database (this repository's branch `db`) is retired: no player used it.

**OSD** (`CONF_STR`), in this order:

```
"<Name>;;",
"-;",
"O[122:121],Aspect ratio,Original,Full Screen,[ARC1],[ARC2];",
"O[125:123],Scale,Normal,V-Integer,Narrower HV-Integer,Wider HV-Integer,HV-Integer;",
"O[4:2],Scandoubler Fx,None,HQ2x,CRT 25%,CRT 50%,CRT 75%;",
"H5O[1],HDMI Only,No,Yes;",                    hidden with direct_video
"H4P1,CRT Options;",                           hidden with forced_scandoubler
"P1-;",
"P1O[68:64],Horizontal Size,0,+1,...,+7,-24,...,-1;",
"P1O[72:69],Horizontal Pos,0,+1,...,+7,-8,...,-1;",
"P1O[76:73],Vertical Pos,0,+1,...,+7,-8,...,-1;",
"P1-;",                                        the HUD lines: only for an edge the game has a HUD at,
"P1O[81:77],HUD (Top),10,11,...,24,0,...,9;",   none of the three without a HUD (see HUD margins)
"P1O[86:82],HUD (Bottom),10,11,...,24,0,...,9;",
"O[6:5],Stereo Mix,None,25%,50%,100%;",
"O[15],FPS Counter,Off,On;",
"-;",
"O[10:7],Sound Volume,100%,90%,...,0%;",      only if the game mixes sound and music separately
"O[14:11],Music Volume,100%,90%,...,0%;",
"O[..],Resolution,320x200,640x200;",          first of the game options, if the game can render more than one
<game options, status bits 24..63>
"-;",
"J1,<8 game actions>,Menu OK,Menu Back;",
"jn,<the 8 buttons A,B,X,Y,L,R,Select,Start in the order of the actions>;",
"V,v",`BUILD_DATE
```

- **Triggers**, lines that do something once each time they are selected (a cheat, "next level"): `T[56]` to `T[59]` only, which the game reads with `MH_OSDTriggers()` (host version 13; Main_MiSTer sets a trigger's bit for microseconds, so the core flips it instead and the game sees the change).
- Status bits 0..23 mean the same in every core (`MH_OSD_*` in `mister_hybrid.h`), bits 24..63 belong to the game, bits 64..86 are the *CRT Options* page, the same in every core, 96..103 the MT32-pi's lines (host version 15, in a core whose games play MIDI: [MIDI and the MT32-pi](#midi-and-the-mt32-pi)); the game reads them with `MH_OSDStatusHigh()` (host version 13), for the HUD margins. Bit 15 is the *FPS Counter*: the library draws the frames shown in the last second into the top left corner of every frame it hands to the core (`MH_Present()`, so SDL games too), and the game does nothing for it. Bits 16..23 are unused (*Menu OK* / *Menu Back* up to host version 13).
- *HDMI Only* and *CRT Options* are the same lines in every core, whether or not its game has a mode they matter for: see [Video modes](#video-modes). *HDMI Only* is bit 1 since host version 13. Bit 0 is never an option: Main_MiSTer holds it as the core's reset while it loads the core and clears it after, so *HDMI Only*, which was there, was off after every load. The menu mask bits 4 and 5 that hide them are the core's; a game has bits 0..3 (`MH_SetMenuMask()`).
- The first entry of every option is its default, so a fresh install needs no settings.
- An option the game cannot honour is left out. One that depends on the data the player installed (a resolution this copy of the game has no graphics for) is replaced by a greyed line that says what is in use, with `MH_SetMenuMask()` and `H`/`D` flags in the `CONF_STR` (see VanillaConquer's *Resolution*).
- *Resolution* is the game's to read: it sizes its window (or calls `MH_SetMode()`) and the core follows. It applies while the game runs, without a restart. The shared screens are always 320x200.

**Controls**

- Keyboard and mouse work as in the original game.
- The `J1` list has the game's eight most important actions and maps all eight buttons of MiSTer's default pad (`jn`), so every button does something and the layout shows up in "Define buttons". The last action is the one that opens the game's menu, on Start.
- In menus the d-pad and left stick move, and confirm/back are *Menu OK* / *Menu Back*: the buttons of the MiSTer menu, whatever they do in the game. For each controller they are its *Menu OK* / *Menu Back* of Main_MiSTer's *Define joystick buttons*, else the A / B of its mapping there, as in the MiSTer menu; a controller without a mapping confirms and goes back as on its console (vendor `057e`, Nintendo and 8BitDo pads in Switch mode: the right button and the bottom one; every other: the bottom one and the right one). `MH_MenuButtons()` resolves them (the SDL driver delivers them as buttons 28 and 29). Up to host version 13 the OSD had *Menu OK* / *Menu Back* options to pick other buttons (status bits 16..23, unused since: a saved setting may still have them set).
- Nothing requires a keyboard: lists, confirmations and name entry work with the pad.

**Behaviour**

- Loading the core starts the game; there is no file to pick in the OSD.
- Until the game shows its first frame, and after it left, the core is in 640x480 (interlaced at 15kHz, progressive at 31kHz with `forced_scandoubler=1` or *HDMI Only*) and shows a picture of its own: the MiSTer logo in the middle of a purple screen and, at the bottom right, "Hybrid Core Framework vN" (N is the host version) over "by danik", next to the danik icon, 34 pixels from the right edge and from the bottom, where a screen with overscan still shows them. So a core without its game is recognisable. A game that was killed or was running when the MiSTer was reset leaves its control block behind, so the core clears that when it starts and never takes it for a game. When the core is loaded the picture comes with an intro (host version 13), after a quarter of a second of black for the screen to lock: the background fades in (0.5 seconds), the logo rises 16 pixels into place while it fades in (0.5), the dots under "MiSTer" start lighting up in turn (and go on until the game has the screen), the icon slides in from the right edge (0.4 seconds later, 0.5), the text slides 20 pixels out from behind it while it fades in (0.5) and a shine sweeps evenly across the logo (1.6): 4.1 seconds, the other movements on an ease-out curve. A game that shows its first frame before the end waits for it; then the picture fades out in 0.7 seconds and only then the game gets the screen and its video mode. A game that comes while the screen is still black gets it at once. After a game has left, the screen stays black for 3 seconds before the picture comes back, with its intro: leaving normally ends in the MiSTer menu, and the picture is not meant to flash up on the way. The call that shows the first frame waits for all that, so no frame is lost; a game built for an older host waits 1.5 seconds at most and shows its first frames to nobody after that. `sim/wall_gif.sh` makes a GIF of the intro from the simulated core, `sim/wall_shot.sh` a PNG of its end. The picture is in the FPGA: `tools/make_wall.py` makes `rtl/hybrid_wall*.hex` from `assets/MiSTer.png` (429x111), `assets/danik.png` (53x54) and the `VERSION` of `hybrid_host.sv`, 16 colours for the logo and 16 for the rest; run it again when one of them changes (`--preview <file.png>` writes the screen as it is at the end of the intro, without the dots), and the map of the dots and the ease-out curve with them. The intro's timing is in `hybrid_host.sv` (`WALL_T_*`, `WALL_D_*`).
- A game with several data sets (or nothing but a choice to make before it starts) asks with `MH_UI_Menu()`. Problems the player can fix (missing data) are shown with `MH_UI_Message()` in plain words that say which files go where, never left in the log only: see [Messages](#messages).
- Quitting from the game's menu returns to the MiSTer menu (or to the game list if the player came from it). The launch script loads the menu core only if `/tmp/CORENAME` still names this core, and not while a message is up: a game that crashed or left with an error gets one ("The game stopped unexpectedly", `mh_message`), and the core stays with it until the player leaves.
- The game leaves when another core is loaded (`MH_CheckAlive()`, `SDL_QUIT`) and never touches the shared memory afterwards.
- The launcher runs the game with `nice -n -20 taskset 0x03`; the game thread takes CPU0, audio and the frame copy run on CPU1. The priority matters: the rest of the system runs on CPU0 too, and a game that is to show one frame per field cannot wait for it.
- `touch /tmp/<name>_nolaunch` keeps the core loaded without starting the game, for development.
- Without the launcher running, the core says so by itself after 12 seconds ("Game not running. Please run danik_hybrid_ports.sh from the Scripts folder.").

**Documentation**

- `README.md` in the repository and `README.txt` in the package have the same sections in the same order: Requirements, Installation, OSD options, Controls (keyboard, then a table of the default gamepad mapping with the MiSTer, Xbox and PlayStation names), Building, Credits, License. `README.md` also has a one line Support section with the Patreon link before Credits.

## Credits

- **[MiSTer Frontier](https://github.com/MiSTerOrganize/MiSTer_Frontier)** by MiSTer Organize: thank you for the inspiration. Hybrid cores on the MiSTer, and the way their game is launched (a daemon that watches the loaded core and runs a script from its games folder), come from MiSTer Frontier. The code here is a separate implementation and does not need MiSTer Frontier installed.
- **[JTOPL](https://github.com/jotego/jtopl)** by Jose Tejada (jotego): the FM chip, GPL-3. `rtl/jtopl/` is a copy of the files a YM3812 needs, unchanged.
- **[Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3)** by Nuke.YKT: the chip `fakecore` uses on the PC, LGPL-2.1.
- **[MiSTer](https://github.com/MiSTer-devel)** by Sorgelig and the MiSTer-devel contributors: the framework the cores are built on.
- **[SDL](https://libsdl.org)** by Sam Lantinga and contributors.

## License

[GPL-3.0](LICENSE), except where a file says otherwise: `rtl/hybrid_host.sv` is GPL-2.0-or-later like the MiSTer framework it is built into, and the SDL drivers in `sdl2/` are under the zlib license like SDL.
