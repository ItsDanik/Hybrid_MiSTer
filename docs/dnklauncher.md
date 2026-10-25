# dnkLauncher: a frontend core for the hybrid ports

Danik's idea, 2026-10-25; the decisions below are Danik's of the same day, from an interview. Nothing of it is built yet.

## Script or core

The largest decision, researched before the interview (Main_MiSTer 8328d9f of 2026-10-25, our launcher's screen `launcher/ui/danik_hybrid_ports_ui.c`, the MiSTer forum):

| | Script (menu core, from *Scripts*) | Core (a hybrid core whose program is the frontend) |
|---|---|---|
| Reached from | the OSD's *Scripts* page only: an MGL loads a core, never a script; cannot be `bootcore` | a menu entry like any core, in the player's layout |
| 15kHz CRT | Main nags "modify MiSTer.ini" (`fb_terminal=2` or `vga_scaler=1`, `menu.cpp` `vga_nag()`) | `hybrid_host` video: 15kHz, interlace rules, *CRT Options*, *HDMI Only*, as every port |
| Picture | the menu core's, whatever the player's `video_mode` is | the purple screen and the ports' look |
| Input | the pad as keys (d-pad, A B X Y L R; no Start, Select or sticks) | the core's `J1` mapping, Menu OK / Back, the OSD hold |
| Sound | none | the core's (not used: below) |
| After a game | the MiSTer menu | the frontend again (below) |
| Cost | none to open | one FPGA load to open it (a second or two, not measured) and one more core to keep at the host version |

Decided: **a core**. A script's two worst points (no menu entry, the MiSTer.ini change on CRTs) cannot be fixed by its program. `load_core <file>.mgl` on `/dev/MiSTer_cmd` starts a game from either (`input.cpp`, MGL and MRA both taken).

## Decisions

- **Name**: the engine is `dnkLauncher` (`CONF_STR`, `games/dnk/dnkLauncher/`, `logs/dnk/dnkLauncher/`); the menu entry is **Hybrid Ports**: `_Other/dnk-Hybrid Ports.mgl`, or `Hybrid Ports.mgl` in the other layouts.
- **Scope**: the hybrid ports only, every game of every port the database has. No other cores, no ROMs.
- **The launcher's screen is folded in**: installing and removing ports and games, *Launcher* on / off, *Menu entries*, Apply, are a page of the frontend. `danik_hybrid_ports.sh` from *Scripts* stays, for the first setup and SSH (`dialog`). The daemon stays as it is: it is what runs a port's program when its core is loaded.
- **Reached by** its menu entry; **games started from it return to it** when they quit; **every game's own menu entry stays** next to it. No `bootcore`.
- **Video 640x240**, progressive at 15kHz. Its one mode: the frontend is not a game, so the rule that a core starts at 320x200 does not apply to it (to confirm). No HUD, so no HUD lines; *HDMI Only* and *CRT Options* as in every core.
- **No sound.** `MIDI_SYNTH` 0.
- **Its own start: no intro.** The core shows the purple background and badge without the MiSTer logo's 4.1 seconds, and the list comes as soon as its program is up.
- **Layout: a list and a preview** (below).
- **Sort modes, one list**: a button cycles A-Z, By port, Recent, Favourites. The mode is remembered.
- **Per game**: its logo, **a gameplay screenshot of its own** (a new asset for every game, as the logos were), year and developer, a short description, the port and its version.
- **Games whose data is missing** are listed, dimmed and marked; the preview says which files go where, the text of the core's own message.
- **A game started from the frontend starts shorter**: no MiSTer-logo intro, the data check, "Loading", the game's logo as now. Its own menu entry keeps the full start.

## The screen

```
+--------------------------------------------------------------+
| HYBRID PORTS                              Sort: A-Z    41    |
| > Doom                 | +---------------------------------+ |
|   Doom - SIGIL         | |           game logo             | |
|   Doom II              | +---------------------------------+ |
|   Duke Nukem 3D        | +---------------------------------+ |
|   Heretic              | |                                 | |
|   Hexen        missing | |         gameplay screenshot     | |
|   Quake                | |                                 | |
|   Wolfenstein 3D       | +---------------------------------+ |
|   ...                  | 1993 - id Software                  |
|                        | The original. ...two lines...       |
|                        | CrispyDoom 20261022                 |
| A: Play  X: Favourite  Select: Sort  Y: Ports   B: Exit      |
+--------------------------------------------------------------+
```

- The preview pane is about 300 of 640 pixels wide. A 4:3 picture that wide is 112 lines of 240; the logo (440x200 at square pixels, so 440x100 in 640x240) at that width is about 300x68. With the text, that is more than 240 lines with a status line: the mock-up (phase 1) settles the sizes, maybe a shorter description or a smaller logo.
- **Controls**, after the rules "menus work with Menu OK and Menu Back": Menu OK (and A) plays, Menu Back (and B) leaves to the MiSTer menu (`load_core /media/fat/menu.rbf`), X marks a favourite, Select cycles the sort mode, Y the ports page, L / R a page up and down. The left stick moves like the d-pad.
- **The ports page**: the launcher's screen of today (ports and games with tickboxes, *Launcher*, *Menu entries*, Apply), each port with its first game's logo. Apply runs what the script runs (`danik_hybrid_ports.sh download`, the filter it writes), Downloader's output in a box as `danik_hybrid_ports_ui run` shows it. The decisions stay in the script, as today; the frontend is the screen. An update of the frontend itself, during Apply, takes effect when it is next loaded.

## Where everything comes from

- **The list**: the launcher's catalogue (`Scripts/.danik_hybrid_ports/catalogue.txt`, every published port and game) for the ports page; for the game list, the games installed (`games/dnk/<Engine>/games.txt` there).
- **The pictures are the frontend's own**, made once and never scaled on the MiSTer: `distribution.py` makes them when it publishes (as it makes the catalogue), from every published port's repository, and the frontend brings them in `games/dnk/dnkLauncher/art/`: per game the logo and the screenshot scaled for the pane (a good filter, then dithered), raw RGB565 at the size they are drawn. About 300x68 and 300x112 pixels, 40 KB and 67 KB, so about 5 MB for the 45 or so games there are. They are the database's launcher files (tag `launcher`), so every player who has the launcher has them, and an already published port shows its art without being packaged again; the ports page can show ports that are not installed.
- **New assets in each core's repository**: `package/screens/<id>.png` for every game of `games.txt` (gameplay, never a menu: looked at before it goes in), and `package/info.txt`, a line per game, `<id>|<year>|<developer>|<description>`. `games.txt` stays as it is (`MH_PickGame()`, `distribution.py` and older launchers read it). Required for every game, as the logos are: `distribution.py` refuses a port without them once the frontend is out, and a game without them meanwhile has the port's `docs/screenshot2.png` and no text.
- **Data missing**: today a game's files are known only to its port's C code (`MH_Game.files`, jfSW checks by checksum, `MH_NOT_THERE`). Proposed: a mode of the library, `<program> --check`, which does `MH_PickGame()`'s check without the core and prints for each game whether it is there, and its message if not. The frontend runs it for every port in the background once the list is up and keeps the result until the game's folder changes (its time). A port built before it has no marks. The other way, the patterns written in `info.txt` too, would be the same thing in two places.
- **Recent**: the daemon writes every game it starts, from the frontend or its own entry, into `games/dnk/dnkLauncher/recent.txt` (set name and time; a launcher change, its `VERSION`). **Favourites** and the sort mode: `games/dnk/dnkLauncher/settings.txt`, the frontend's.

## Starting a game and coming back

1. The frontend writes `/tmp/dnk_launched` (the set name) and sends `load_core <the game's menu entry>` to `/dev/MiSTer_cmd`: the MGL of the player's layout, so the set name and the OSD settings are the game's.
2. **Shorter start**: the game core's RTL starts its intro as always; the library, at `MH_Open()`, finds the file and tells the core to end it (a bit of the control block: the MiSTer logo goes now, as at the intro's end), and `MH_GameLogo()` waits for that instead of field 300. The daemon looks for a core change every 0.5 seconds; for this it can look more often while the file is there. About 6.5 seconds from the load to the first frame instead of 9.1 (the intro cut at about 1.5, the two lines 0.8, the handover 1.5, the logo's 2 seconds, the fade-out 0.7). Leaving out the logo's 2 seconds as well would make it about 4.5; not asked for.
3. **Back**: a game that quits loads the frontend's menu entry instead of `menu.rbf` while the file names its set (`hps/mister_hybrid.c`, `load_core /media/fat/menu.rbf`), and removes the file. Quitting a game picked from a core's own list still brings that list back (exit code 42). Leaving the core from the OSD, or loading another, removes the file too (the daemon, when the core changes to one that is not the game's).
4. Both are a framework change, host version 18: a port has them once it is built again. Until then a game started from the frontend starts in full and quits to the MiSTer menu.

## Phases

1. **Mock-ups** on the PC: the list, the preview, the ports page, a game without data, at 640x240 as a CRT and an HDMI screen show it. Danik decides the sizes.
2. **Assets**: `info.txt` and `screens/` for the published ports' games; the art set in `distribution.py`.
3. **The core and its program** (no framework change): `new_core.sh dnkLauncher` (no game options, `MIDI_SYNTH` 0, no HUD lines, the core without the intro), the program in C on the `MH_` library (not SDL: it draws a list), the list, sort modes, favourites, starting a game, the ports page. Recent in the daemon. Tested with `tools/test_on_mister.sh` as every core, and the ports page on a copy of an SD card (`DANIK_HYBRID_ROOT`).
4. **Host version 18**: the shorter start, the way back, `--check`. Cores take it at their next build.
5. **Documentation**: the README section *The launcher* (its text in `docs/readme.md`, the template's and every port's) and its picture, `OVERVIEW.md`, the frontend's own README; publishing when Danik asks.

## Open questions

- The 640x240 exception to "video starts at 320x200" (above).
- `--check`, or the patterns in `info.txt`.
- Who writes the descriptions and takes the screenshots: drafts by Claude from the running games (`fakecore`, `MISTER_HYBRID_FPS=0`), Danik reads them over.
- Players read "the launcher" for the Scripts screen today (the README section *The launcher*); with the screen in the frontend, the section is about the frontend, the Scripts entry a line in it.
