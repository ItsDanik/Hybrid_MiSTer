# MiSTer hybrid core framework

What every hybrid core shares. A hybrid core runs an open source game on the MiSTer's ARM CPU (the HPS) and uses a small FPGA core for what the ARM side has no hardware for: native 15kHz video, audio and the MiSTer's input devices. The two halves talk through shared DDR3 memory.

This repository is the `hybrid/` submodule of every hybrid core repository, so they all use the same code. A game repository adds the game, its `CONF_STR` and its `danik_hybrid_launch.sh`.

Cores built on it: [ECWolf](https://github.com/ItsDanik/ecwolf_MiSTer) (Wolfenstein 3D). [Dethrace](https://github.com/ItsDanik/Dethrace_MiSTer) (Carmageddon) uses the launcher and has its own, older copy of the rest.

| Path | |
|---|---|
| `rtl/` | The FPGA side. `hybrid_host.sv`: scanout from DDR3 in the video mode the game sets (8bpp paletted or RGB565, triple buffered), at 15.6kHz / 59.6Hz wherever the mode allows it, 44.1kHz audio ring, keyboard, mouse, joysticks and OSD status published every vblank. Its header documents the shared memory layout and the video timings. `hybrid_vclk.sv` and `pll_vid` make its video clock, which changes with the mode and the CRT options. A core takes all of it with `hybrid.qip`. `sim/run.sh` runs the testbenches. |
| `hps/` | The ARM side as a small C library (`mister_hybrid.h`): attach to the core, present frames, palette, input, audio, the OSD's Menu OK/Back resolution, and the shared screens (game list, error message). |
| `sdl2/` | SDL2 video, audio and input drivers on top of `hps/`, and the script that builds a static SDL2, SDL2_mixer and SDL2_net with them. An SDL2 game needs little more than a recompile. |
| `launcher/danik_hybrid_cores.sh` | What starts the games on the MiSTer: a daemon that runs `games/<core name>/danik_hybrid_launch.sh` while that core is loaded. The user runs it once from the Scripts menu; it registers itself in `user-startup.sh`. Its header documents the contract for a `danik_hybrid_launch.sh`. See [The launcher](#the-launcher) for how it reaches the MiSTer and is kept up to date. `make_db.py` builds the Downloader database for it. |
| `toolchain/` | Docker images: ARM cross compiler matching the MiSTer's glibc, HDL simulation tools. `docker.sh` runs a command in the toolchain. |
| `template/`, `new_core.sh` | The start of a new core repository: FPGA core, build, package and deploy scripts, launch script and READMEs with the name filled in. See [Porting a game](#porting-a-game). |
| `tools/fakecore.c` | Stand-in for the FPGA core on the PC: runs the game against a file instead of the DDR3 window, feeds scripted input, takes screenshots and records audio. |
| `tools/*.py` | For tests on the MiSTer over ssh: `uinput_kbd.py` and `uinput_pad.py` press keys and gamepad buttons through Main_MiSTer, `status.py` prints what the core publishes. |

## The launcher

`danik_hybrid_cores.sh` is the one thing all hybrid cores share on the SD card. It is developed here and nowhere else; a core takes it from this submodule when its release is packaged.

- **It comes with every core.** A release has it as `Scripts/danik_hybrid_cores.sh` and again as `games/<Name>/danik_hybrid_cores.sh`. The user extracts the zip and runs it once from the Scripts menu; there is nothing separate to install.
- **The newest copy wins.** The script has a `VERSION`. The daemon looks at the copies in `games/*/` when it starts and every 10 seconds while no game runs, installs the one with the highest version over the file in `Scripts` and restarts itself with it. Installing an older core after a newer one therefore cannot bring an old launcher back, and an updated launcher needs no second run from the Scripts menu.
- **Updates without a core release** (optional): with this entry in `downloader.ini`, `update_all` keeps the launcher current.

  ```ini
  [ItsDanik/Hybrid_MiSTer]
  db_url = https://raw.githubusercontent.com/ItsDanik/Hybrid_MiSTer/db/db.json.zip
  ```

  The database is rebuilt by `.github/workflows/db.yml` whenever `launcher/` changes on `main`.
- **Changing it:** raise `VERSION` with every change that reaches users, keep the last line as it is (the daemon replaces the file while it runs), and keep the contract in the header working for the launch scripts of cores that are already released.

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
- Audio is converted to 44.1kHz stereo by SDL; the core's sample clock paces the audio thread.
- Keyboard and mouse arrive as SDL events. Joysticks 1 and 2 are SDL joysticks with the d-pad as hat 0, the sticks as axes 0-3 and the buttons of the core's `J1` list as buttons 0.. in that order. Buttons 28 and 29 are Menu OK and Menu Back (see below).
- `SDL_QUIT` arrives when another core is loaded.
- `#include <mister_hybrid.h>` for the rest: OSD options (`MH_OSDStatus()`), the shared screens (`MH_UI_Menu()`, `MH_UI_Message()`).

**A game without SDL:** use `hps/` directly, as the SDL drivers do (`sdl2/SDL_mistervideo.c` and `SDL_misteraudio.c` are the reference).

**The FPGA core:** `core/` from the template (Template_MiSTer's `sys/`, a 50MHz PLL, `hybrid_host` with its video clock from `rtl/hybrid.qip`, `video_mixer`, `video_freak`). Only the `CONF_STR` in `core/<Name>.sv` is the game's.

**The game's own file:** `template/game/mister_port.c` is a starting point for what the SDL drivers cannot do for the game: its OSD options, frame pacing, the game list, errors on the screen and the exit code.

**Frame pacing:** the core shows 59.64 fields per second and takes the newest frame at every vblank (with an interlaced mode at every second one; 800x600 and 1024x768 are 60.3 and 60.0 frames per second). A game that renders at another rate, or by the system clock, drops or repeats frames at regular intervals. Show one frame each time the core takes one: wait with `MH_WaitFrame()` (or the `SDL_MISTER_VSYNC` hint) and take the game's time from `MH_FieldCounter()`, not from the system clock. A game with a fixed logic rate that is not the field rate (Wolfenstein 3D: 70 per second) has to draw between two steps of its logic as well, see the ECWolf core.

**Testing without a MiSTer:** `gcc -o fakecore tools/fakecore.c`, start `fakecore <file> [script]`, then the PC build of the game with `MISTER_HYBRID_SHM=<file>`. It shows every mode as a plain picture: the two forms of a mode, the CRT options and the analog output are the FPGA's and can only be seen on a MiSTer (`tools/status.py` prints the mode and the video clock the core has measured).

## Video modes

| Mode | 15kHz screen | VGA monitor or *HDMI Only* |
|---|---|---|
| 320x200 (the default), 640x200, 320x240, 640x240 | 15.6kHz, 59.6Hz | the same; the core's scandoubler makes it 31kHz for a VGA monitor |
| 640x400, 640x480 | interlaced at 15.6kHz, 29.8 frames per second | progressive at 31.25kHz, 59.5Hz |
| 800x600, 1024x768 | not available | 37.9kHz / 60.3Hz, 48.4kHz / 60.0Hz |

A game asks for a mode (`MH_SetMode()`, or the size of its SDL window) and the core picks the form. Which screen is there is the player's to say, with one of three settings:

| Setting | How | 640x400 and above |
|---|---|---|
| 15kHz (default) | nothing set | interlaced at 15kHz |
| VGA | `forced_scandoubler=1` in `MiSTer.ini` | progressive at 31kHz and more on the analog output and HDMI |
| HDMI only | OSD option *HDMI Only* | progressive on HDMI, the analog output switched off |

- **A 15kHz screen never gets more than 15kHz.** *HDMI Only* does not send the progressive signal to the analog output: it switches that output off (`VGA_DISABLE`) for as long as such a mode is shown. The 200 and 240 line modes still go there, with the OSD over them. With `direct_video=1` the HDMI output itself feeds an analog screen, so the option is hidden and ignored.
- Interlaced, a frame is on the screen for two fields, its even rows in the first and its odd rows in the second, and a new frame is only taken between two frames. On a CRT fine horizontal lines flicker; the HDMI scaler weaves the fields into a steady picture, which is what *HDMI Only* is for: the same modes progressive.
- 800x600 and 1024x768 have no 15kHz form. `MH_ModeAvailable()` tells whether they can be shown, and a game offers them only then. If the player changes the setting while one is shown, the screen is black until the game sets another mode (the SDL driver does that by itself).
- A change between the forms, or to and from 800x600 and 1024x768, changes the video clock. The video output is off (no sync) for the few milliseconds that takes, and only comes back when the core has measured the new clock to be right.
- **CRT Options** (not with `forced_scandoubler=1`) adjust the 15kHz forms to the screen without touching the pixels: *Horizontal Size* makes the video clock and the number of pixels in a line 1% smaller or larger per step, so every pixel gets wider or narrower by the same amount and the line stays 64us long, from +7 (where the blanking gets short) down to -24 (a picture four fifths as wide), with the picture staying where it is; *Horizontal Pos* (steps of 2 pixels of 320) and *Vertical Pos* (lines) move the sync pulses. Positive is larger, to the right and down. The position stops where the blanking would get too short, which is early for 240 lines, where 22 lines are all there is. The height cannot be changed that way: it is the number of lines, and another number of lines is another refresh rate. None of this changes the HDMI picture.
- 1024x768 has its own, larger framebuffers; everything else is where it was, so a game built for an older host works with a newer core and the other way round, with the modes they both know.

## Conventions

These make the cores look and work alike. Where the framework can enforce one, it does.

**Naming and layout on the SD card**

- One name everywhere: core name in `CONF_STR`, `_Other/<Name>_YYYYMMDD.rbf`, `games/<Name>/`, `logs/<Name>/`, binary `games/<Name>/<Name>`.
- `games/<Name>/danik_hybrid_launch.sh` is the launcher `danik_hybrid_cores` runs. Everything the game writes (settings, saves) stays in `games/<Name>/`; the log goes to `/media/fat/logs/<Name>/`.
- Game data is never shipped. The README names the files to copy and where.
- Release: `<Name>_YYYYMMDD.zip` to extract at the root of the SD card, containing `_Other/`, `games/<Name>/` with `README.txt` and the licenses, and the launcher from `launcher/` twice: `Scripts/danik_hybrid_cores.sh` and `games/<Name>/danik_hybrid_cores.sh` (see [The launcher](#the-launcher)). Nothing else has to be installed; the README lists running `danik_hybrid_cores` once from the Scripts menu as a requirement.

**OSD** (`CONF_STR`), in this order:

```
"<Name>;;",
"-;",
"O[122:121],Aspect ratio,Original,Full Screen,[ARC1],[ARC2];",
"O[125:123],Scale,Normal,V-Integer,Narrower HV-Integer,Wider HV-Integer,HV-Integer;",
"O[4:2],Scandoubler Fx,None,HQ2x,CRT 25%,CRT 50%,CRT 75%;",
"H5O[0],HDMI Only,No,Yes;",                    hidden with direct_video
"H4P1,CRT Options;",                           hidden with forced_scandoubler
"P1-;",
"P1O[68:64],Horizontal Size,0,+1,...,+7,-24,...,-1;",
"P1O[72:69],Horizontal Pos,0,+1,...,+7,-8,...,-1;",
"P1O[76:73],Vertical Pos,0,+1,...,+7,-8,...,-1;",
"O[6:5],Stereo Mix,None,25%,50%,100%;",
"-;",
"O[10:7],Sound Volume,100%,90%,...,0%;",      only if the game mixes sound and music separately
"O[14:11],Music Volume,100%,90%,...,0%;",
"O[..],Resolution,320x200,640x200;",          first of the game options, if the game can render more than one
<game options, status bits 24..63>
"-;",
"O[19:16],Menu OK,MiSTer,A,B,X,Y,L,R,Select,Start;",
"O[23:20],Menu Back,MiSTer,A,B,X,Y,L,R,Select,Start;",
"-;",
"J1,<8 game actions>,Menu OK,Menu Back;",
"jn,<the 8 buttons A,B,X,Y,L,R,Select,Start in the order of the actions>;",
"V,v",`BUILD_DATE
```

- Status bits 0..23 mean the same in every core (`MH_OSD_*` in `mister_hybrid.h`), bits 24..63 belong to the game, bits 64 and up never reach the game and are for the core's video options. Bit 1 and bit 15 are free.
- *HDMI Only* and *CRT Options* are the same lines in every core, whether or not its game has a mode they matter for: see [Video modes](#video-modes). The menu mask bits 4 and 5 that hide them are the core's; a game has bits 0..3 (`MH_SetMenuMask()`).
- The first entry of every option is its default, so a fresh install needs no settings.
- An option the game cannot honour is left out. One that depends on the data the player installed (a resolution this copy of the game has no graphics for) is replaced by a greyed line that says what is in use, with `MH_SetMenuMask()` and `H`/`D` flags in the `CONF_STR` (see VanillaConquer's *Resolution*).
- *Resolution* is the game's to read: it sizes its window (or calls `MH_SetMode()`) and the core follows. It applies while the game runs, without a restart. The shared screens are always 320x200.

**Controls**

- Keyboard and mouse work as in the original game.
- The `J1` list has the game's eight most important actions and maps all eight buttons of MiSTer's default pad (`jn`), so every button does something and the layout shows up in "Define buttons". The last action is the one that opens the game's menu, on Start.
- In menus the d-pad and left stick move, and confirm/back are *Menu OK* / *Menu Back* from the OSD: by default the same buttons as in the MiSTer menu, whatever they do in the game. `MH_MenuButtons()` resolves them (the SDL driver delivers them as buttons 28 and 29).
- Nothing requires a keyboard: lists, confirmations and name entry work with the pad.

**Behaviour**

- Loading the core starts the game; there is no file to pick in the OSD.
- Until the game shows its first frame, and after it left, the core is in 320x240 and shows a picture of its own: the MiSTer logo in the middle of a purple screen and "danik HCF vN" (N is the host version) at the bottom right, 24 pixels from the right edge and 20 lines from the bottom, where a screen with overscan still shows it. So a core without its game is recognisable. A game that was killed or was running when the MiSTer was reset leaves its control block behind, so the core clears that when it starts and never takes it for a game. The picture fades in from black when the core is loaded (a quarter of a second of black for the screen to lock, then 0.7 seconds) and fades out as slowly when the game shows its first frame; only then the game gets the screen and its video mode. After a game has left, the screen stays black for 3 seconds before the picture comes back: leaving normally ends in the MiSTer menu, and the picture is not meant to flash up on the way. The call that shows the first frame waits for the fade-out, so no frame is lost; a game built for an older host fades the same way, without the wait. The picture is in the FPGA: `tools/make_wall.py` makes `rtl/hybrid_wall*.hex` from the logo (a PNG of 244x64 that is not in the repository) and the `VERSION` of `hybrid_host.sv`; run it again when that changes.
- A game with several data sets (or nothing but a choice to make before it starts) asks with `MH_UI_Menu()`. Problems the player can fix (missing data) are shown with `MH_UI_Message()` in plain words that say which files go where, never left in the log only.
- Quitting from the game's menu returns to the MiSTer menu (or to the game list if the player came from it). The launcher loads the menu core only if `/tmp/CORENAME` still names this core.
- The game leaves when another core is loaded (`MH_CheckAlive()`, `SDL_QUIT`) and never touches the shared memory afterwards.
- The launcher runs the game with `nice -n -20 taskset 0x03`; the game thread takes CPU0, audio and the frame copy run on CPU1. The priority matters: the rest of the system runs on CPU0 too, and a game that is to show one frame per field cannot wait for it.
- `touch /tmp/<name>_nolaunch` keeps the core loaded without starting the game, for development.

**Documentation**

- `README.md` in the repository and `README.txt` in the package have the same sections in the same order: Requirements, Installation, OSD options, Controls (keyboard, then a table of the default gamepad mapping with the MiSTer, Xbox and PlayStation names), Building, Credits, License. `README.md` also has a one line Support section with the Patreon link before Credits.

## Credits

- **[MiSTer Frontier](https://github.com/MiSTerOrganize/MiSTer_Frontier)** by MiSTer Organize: thank you for the inspiration. Hybrid cores on the MiSTer, and the way their game is launched (a daemon that watches the loaded core and runs a script from its games folder), come from MiSTer Frontier. The code here is a separate implementation and does not need MiSTer Frontier installed.
- **[MiSTer](https://github.com/MiSTer-devel)** by Sorgelig and the MiSTer-devel contributors: the framework the cores are built on.
- **[SDL](https://libsdl.org)** by Sam Lantinga and contributors.

## License

[GPL-3.0](LICENSE), except where a file says otherwise: `rtl/hybrid_host.sv` is GPL-2.0-or-later like the MiSTer framework it is built into, and the SDL drivers in `sdl2/` are under the zlib license like SDL.
