# @NAME@ for MiSTer

@TITLE@ on [MiSTer FPGA](https://github.com/MiSTer-devel/Main_MiSTer/wiki) as a hybrid core.

TODO: one paragraph: which source port the game is and where it comes from. It runs on the MiSTer's ARM CPU. The @NAME@ FPGA core provides native 15kHz video (CRT, VGA and HDMI) at 320x200, 44.1kHz audio and keyboard, mouse and gamepad input. The two halves talk through shared DDR3 memory.

> **Beta.** Expect rough edges and please report problems in the issues.

## Requirements

- **danik_hybrid_cores**, the launcher that comes in the release zip (`Scripts/danik_hybrid_cores.sh`): run it **once** from the MiSTer's `Scripts` menu. It starts the game whenever the core is loaded, keeps running after a reboot, and serves all our hybrid cores. Every hybrid core brings the launcher along and the newest version is the one that runs, so it never has to be run again after an update. Without it the core only shows colour bars.
- **Game data**, which is not included: TODO which copy of the game.

## Installation

1. Download the newest `@NAME@_YYYYMMDD.zip` from [releases](releases/) and extract it to the root of your SD card (`/media/fat`). That gives you:
   - `_Other/@NAME@_YYYYMMDD.rbf`, the FPGA core
   - `games/@NAME@/`, the game binary and its launcher
   - `Scripts/danik_hybrid_cores.sh`, the launcher (from [Hybrid_MiSTer](https://github.com/ItsDanik/Hybrid_MiSTer), which can also keep it up to date through `update_all`)
2. Copy the game data to `/media/fat/games/@NAME@/`: TODO which files, where.
3. Run **danik_hybrid_cores** from the `Scripts` menu, if you have not done so before (see Requirements).
4. Load **@NAME@** from the `Other` menu.

The game's log is in `/media/fat/logs/@NAME@/@name@.log`. TODO: where settings and saved games are.

## OSD options

| Option | |
|---|---|
| Aspect ratio, Scale, Scandoubler Fx, Stereo Mix | as in other cores |
| TODO game options | |
| Menu OK, Menu Back | the gamepad button that confirms / goes back in the game's menus, whatever it does in the game. **MiSTer** (default) uses the OK/Back buttons of your MiSTer menu |

## Controls

- **Keyboard and mouse:**

| Key | Action |
|---|---|
| TODO | |

- **Gamepad** (default mapping):

| Button | Action |
|---|---|
| Left stick | TODO |
| D-pad | TODO |
| A (Xbox B / PlayStation Circle) | TODO |
| B (Xbox A / PlayStation Cross) | TODO |
| X (Xbox Y / PlayStation Triangle) | TODO |
| Y (Xbox X / PlayStation Square) | TODO |
| L (LB / L1) | TODO |
| R (RB / R1) | TODO |
| Select | TODO |
| Start | Menu |

Change the buttons in the OSD under *Define @NAME@ buttons*. *Menu OK* and *Menu Back* there are only needed for a button without a game function; MiSTer doesn't let you assign a button twice, so to confirm with a button that also has a game function, pick it in the OSD's Menu OK/Menu Back options instead.

## Building

Everything builds in Docker on a Linux PC.

```sh
git clone --recursive https://github.com/ItsDanik/@REPO@.git
cd @REPO@
./build.sh              # ARM game binary  -> build/mister/@name@
./core/build_core.sh    # FPGA core (Quartus Lite 17.0.2) -> core/output_files/@NAME@.rbf
./package.sh            # release zip      -> dist/@NAME@_YYYYMMDD.zip
```

The toolchain image (Debian bullseye, glibc 2.31 to match the MiSTer) is built from `hybrid/toolchain/` on first use. The first build also downloads and builds SDL2, SDL2_mixer and SDL2_net.

### Repository layout

| Path | |
|---|---|
| `hybrid/` | submodule: [ItsDanik/Hybrid_MiSTer](https://github.com/ItsDanik/Hybrid_MiSTer), what all our hybrid cores share: the FPGA host module, the ARM side library, SDL2 drivers, the launcher (`danik_hybrid_cores.sh`), toolchain and conventions. See its README. |
| `core/` | FPGA core, based on [Template_MiSTer](https://github.com/MiSTer-devel/Template_MiSTer): `@NAME@.sv` (OSD and button names) around `hybrid/rtl/hybrid_host.sv` |
| `@name@/` | submodule: the game, branch `mister`, with the MiSTer changes |
| `package/` | files shipped in the release next to the binary (`danik_hybrid_launch.sh`, README) |
| `releases/` | release packages |

### Development notes

- `./build.sh host` builds the same game for the PC. It runs against `hybrid/tools/fakecore`, a stand-in for the FPGA core that takes scripted input and saves screenshots and audio:
  ```sh
  gcc -O2 -o build/host/fakecore hybrid/tools/fakecore.c
  build/host/fakecore /tmp/shm script.txt audio.raw &
  cd <data folder> && MISTER_HYBRID_SHM=/tmp/shm <repository>/build/host/@name@
  ```
- `touch /tmp/@name@_nolaunch` on the MiSTer keeps the core loaded without starting the game, so you can start a development binary by hand (set `MISTER_HYBRID_CORE=@NAME@`). `/tmp/danik_hybrid_cores.log` shows what the launcher daemon did.

## Credits

- **TODO the game / source port** by its authors.
- **[SDL](https://libsdl.org)** by Sam Lantinga and contributors.
- **[MiSTer](https://github.com/MiSTer-devel)** by Sorgelig and the MiSTer-devel contributors: the framework and Template_MiSTer.
- **[MiSTer Frontier](https://github.com/MiSTerOrganize/MiSTer_Frontier)** by MiSTer Organize: thank you for the inspiration. Hybrid cores on the MiSTer, and the way their game is launched (a daemon that watches the loaded core and runs a script from its games folder), come from MiSTer Frontier. Our launcher is a separate implementation and does not need MiSTer Frontier installed.

This project and its maintainers are in no way associated with or endorsed by TODO the rights holders. It does not include any game data.

## License

The top-level scripts, tools and documentation are licensed under the [GPL-3.0](LICENSE). The components keep their own licenses: the shared framework in `hybrid/` is GPL-3.0 except where its files say otherwise, TODO the game's license, SDL and the SDL drivers in `hybrid/sdl2` are zlib, the FPGA core and the MiSTer framework are GPL-2.0 (`core/LICENSE`, with the core's own sources GPL-2.0-or-later).
