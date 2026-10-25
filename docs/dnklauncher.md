# dnkLauncher: a frontend core for the hybrid ports

Danik's idea, 2026-10-25; the decisions below are Danik's of the same day, from an interview, and the answers to its open questions given the same day. Nothing of it is built yet.

**An extra project, in alpha** (Danik, 2026-10-25: "for now, we will not break anything the users use"). Nothing players have changes for it: the launcher keeps every feature it has, its screen from *Scripts* included, and the READMEs, `docs/readme.md` and its section *The launcher* stay as they are. The frontend is not published (not in the database, not on the launcher's screen) until Danik says so.

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
- **The launcher's screen as a page of the frontend too**: installing and removing ports and games, *Launcher* on / off, *Menu entries*, Apply. The launcher's own screen from *Scripts* (`danik_hybrid_ports.sh`, `danik_hybrid_ports_ui`, `dialog` over SSH) stays as it is, with every feature it has: the page is a second way to the same, never in its place (Danik, 2026-10-25). The daemon stays as it is: it is what runs a port's program when its core is loaded.
- **Reached by** its menu entry; **games started from it return to it** when they quit; **every game's own menu entry stays** next to it. No `bootcore`.
- **Video 640x480**, the mode of the core's own purple screen: interlaced at 15kHz, progressive on HDMI and at 31kHz (the rule "a 15kHz screen never gets more than 15kHz"). Its one mode: the frontend is not a game, so the rule that a core starts at 320x200 does not apply to it (Danik, 2026-10-25). First 640x240; the mock-ups showed 240 lines too tight for the MiSTer logo, the badge and a proper font (Danik, the same day). No HUD, so no HUD lines; *HDMI Only* and *CRT Options* as in every core.
- **The look** (the mock-ups' second round, Danik: "it looks so much better"): the MiSTer logo at the top left and the danik badge at the bottom right, where the core's own screen has it; the purple as a gradient; panels with a rim, light from above and a shadow; the cursor a bar from the pink of "FPGA" to peach, gold stars, cyan for ports and years, coral for missing data.
- **Fonts, the player's choice in *Options***: Chakra Petch SemiBold the default, Nunito ExtraBold, Atkinson Hyperlegible Bold, Rubik Medium and Lexend SemiBold the others (Danik, 2026-10-25). All SIL OFL. Picked from 25 measured for interlace flicker (`tools/dnklauncher_fonts.py`): the smooth fonts flicker least (0.14 to 0.16; the 8x8 font of the hybrid core screens and Terminus 0.24 to 0.25), being lit in both fields at their soft edges.
- **Colour schemes, the player's choice in *Options*** (Danik, 2026-10-26: "I like all of the colour schemes, include all of them"): MiSTer Purple (the default), Midnight, Synthwave, Arcade, Amber Phosphor, Sunset, Ocean and Lavender (the one light scheme). A scheme sets every colour of the frontend: the ground's gradient, the panels and their rim, the text, the cursor's bar and the sort mode's pill, the accent (ports, versions), the favourites' stars and missing data. Each keeps its own bar ("the bar should remain the default colour for each colour scheme"): no separate bar setting. The core's own purple screen stays as it is; the start animation fades from it into the scheme's ground. `THEMES` of `tools/dnklauncher_mockup.py`.
- **A flicker filter, a setting in *Options*** (Danik, 2026-10-25): a vertical [1 2 1] / 4 over the picture, only while 640x480 is interlaced (a 15kHz screen); it brings every font to 0.05 to 0.07. On by default (to confirm).
- **Button symbols after the controller** (Danik, 2026-10-25): a Switch, Xbox or PlayStation controller is recognised by its USB vendor (Nintendo 057e, Microsoft 045e, Sony 054c; the library reads them already, `convention_codes()` of `hps/mister_joymap.c`) and the hints show its own buttons: Switch's letters and − / +, Xbox's coloured letters with View and Menu, PlayStation's shapes with Create and Options. Any other controller has MiSTer's own SNES names, unless the player picks a family in *Options* (*Button symbols*: Auto, Switch, Xbox, PlayStation, SNES). **The actions are by letter** (Danik, 2026-10-25): the same letter on every pad, Play A, Exit B, Favourite Y, Ports X (PlayStation: Cross, Circle, Triangle, Square, in Xbox's places). A Switch has Xbox's layout mirrored across the diagonal, so there all four are mirrored, Play on the right button as Menu OK is in the MiSTer menu for Nintendo's pads; not Play and Exit mirrored and the other two by place, as the first mock-ups had it. Play and Exit are Menu OK and Menu Back: with a mapping of its own in the MiSTer menu a controller has them where that says. A hint is drawn by the button's place (Linux names a pad's buttons so, `BTN_SOUTH` the bottom one). A controller of no known family has its places named as MiSTer names them (SNES: A east, B south, X north, Y west). With several controllers, the one pressed last.
- **Options**, a page of the frontend (Start): *Font*, *Colours*, *Flicker filter*, *Button symbols*, *Sort games* (the mode it starts with), a row each over the whole width (the font's name in the font itself), and under the list a short box with one or two lines of the small font saying what the option under the cursor does: no heading, no examples (Danik, 2026-10-26: a horizontal split, not the list and a panel beside it). Seven rows in sight; a longer list scrolls, with a scroll bar as the game list's; kept in `games/dnk/dnkLauncher/settings.txt`.
- **No sound.** `MIDI_SYNTH` 0.
- **Its own start: no intro.** The core shows its screen as it is at the intro's end (the purple, the MiSTer logo in the middle, the badge) without the logo's 4.1 seconds, and the list comes as soon as its program is up, out of that picture (*Animations*, below).
- **Animations** (Danik, 2026-10-25: "flashy, elegant and fast"): pages, sort modes and the cursor move and fade instead of jumping. Below.
- **Layout: a list and a preview** (below), and **a page per game, *Details*** (Danik, 2026-10-25): the main screen shows the list and the artwork only, larger; A on a game opens its *Details* (the logo, the screenshot, every fact, the description), where A plays and B goes back. More room on the main screen, more to read on the game's page.
- **Sort modes, one list**: a button cycles A-Z, By port, Recent, Favourites. The mode is remembered.
- **Per game**: its logo, **a gameplay screenshot of its own** (a new asset for every game, as the logos were), year and developer, a description, the port and its version.
- **Games whose data is missing** are listed, dimmed and marked; the preview says which files go where, the text of the core's own message.
- **A game started from the frontend starts shorter**: no MiSTer-logo intro, the data check, "Loading", the game's logo as now. Its own menu entry keeps the full start.

## The screen

As `tools/dnklauncher_mockup.py` draws it (640x480; its constants are the sizes):

```
The list                                 Details (A on a game)
+------------------------------------+   +------------------------------------+
| [MiSTer logo]  42 games Hybrid Ports|   | [MiSTer logo]   Details Hybrid Ports|
|      (Select) [A-Z] By port Recent..|   |                                    |
| +----------------+ +---------------+|   | +----------------+ +---------------+|
| | DN3D: Penthou..| |   game logo   ||   | |                | |   game logo   ||
| |*Doom II        | | +-----------+ ||   | |   screenshot   | | Doom II *     ||
| |  Doom II - Mas.| | |screenshot | ||   | |    256x192     | | Year, Develop.||
| |  ... 12 rows   | | |  256x192  | ||   | |                | | Port, Game da.||
| |                | | +-----------+ ||   | +----------------+ +---------------+|
| |                | |               ||   | | The description, up to 4 lines   ||
| +----------------+ +---------------+|   | +----------------------------------+|
| (A) Details (Y) Favourite (X) Ports |   | (A) Play (Y) Favourite (B) Back    |
| (Start) Options (B) Exit    [badge] |   |                            [badge] |
+------------------------------------+   +------------------------------------+
```

- **The main screen** is the list and the preview (the game's logo over its screenshot, 256x192), 310 pixels high between the sort modes and the hints; 12 rows of 24 pixels. No text of the game but its name in the list (a strip of title and description under them was there in the fourth round and went to *Details*; Danik, 2026-10-25). 19 pixels from the top edge and 38 from the bottom, 5 more than before each, for a CRT's overscan.
- **Details** (A on a game; Danik, 2026-10-25): the screenshot top left at the same size as in the preview; top right the logo, the title with a star if it is a favourite, and the facts: *Year*, *Developer*, *Port* (and its version), *Game data* (the folder, `games/dnk/<Engine>/` or the USB drive's, or "not found"), *Menu entry* (where the player's layout puts it) and *Last played*; across the bottom the description, up to 4 lines of 14 pixels (about 340 characters). A plays, Y marks it a favourite, B goes back to the list where it was, L and R go to the game before and after in the list. A game without its data has no Play: its screenshot's place says which files go where.
- **Controls**, after the rules "menus work with Menu OK and Menu Back": Menu OK opens *Details* (and there plays), Menu Back leaves to the MiSTer menu (from *Details*, back to the list) (`load_core /media/fat/menu.rbf`), Y marks a favourite, X opens the ports page (by letter, above), Select cycles the sort mode, Start opens *Options*, L / R a page up and down. The left stick moves like the d-pad.
- **The ports page**: the launcher's screen of today (ports and games with tickboxes, *Launcher*, *Menu entries*, Apply), each port with its first game's logo. Apply runs what the script runs (`danik_hybrid_ports.sh download`, the filter it writes), Downloader's output in a box as `danik_hybrid_ports_ui run` shows it. The decisions stay in the script, as today; the frontend is the screen. An update of the frontend itself, during Apply, takes effect when it is next loaded.

## Animations

A frame per field (59.64 a second), as `tools/dnklauncher_mockup.py` draws them (`anim_*`; animated WebP at the real speed and at a quarter). The program keeps each part of a screen as a layer of its own (the ground, the logo, the title, the sort modes, each panel with what is in it, the hints, the badge) and only puts them together, moved and faded, once per field: nothing is drawn anew while an animation runs but a row's text when the list changes. Curves: ease-out (cubic) for what arrives, ease-in-out for what leaves or crosses.

- **Start** (48 fields, 0.8 s): from the core's own screen, the frame its program takes over. The flat purple becomes the gradient, the MiSTer logo goes from the middle to the top left at half its size, the badge from the core's place to the footer's, smaller; the title and the sort modes slide in, then the panels rise 18 pixels into place one after the other (the list, the preview, the hints).
- **The cursor** (7 fields per step): the bar glides to the next row, the rows under it inked as under the bar wherever it is; the list scrolls only when the bar comes within two rows of its edge. The preview crosses over to the next game, its logo sliding in 10 pixels. Held down, each step starts from where the last one is.
- **Sort mode** (Select, 14 fields): the pink pill slides to the next mode; the old list leaves 24 pixels to the left and fades, the new one comes from the right. The cursor stays on the same game.
- **A page** (*Options*, the ports page; 17 fields): the main screen's panels sink 12 pixels and fade one after the other, the page's rise 14 pixels; the count becomes the page's name, the hints cross over. The logo, the title and the badge stay. Back is the same, the other way.
- ***Details*** (A, 22 fields): the screenshot travels from the preview to its place in *Details* (the same size, so it only moves) and the logo to the top of the facts' panel, scaled; the list leaves 28 pixels to the left and fades, the sort modes go up and fade, and *Details*' panels come up around the screenshot, the facts after it has passed them, the description last. B plays it backwards.
- **Play** (A in *Details*, 38 fields): a pink glow round the screenshot flashes, the rest of the screen goes dark while the screenshot grows from its box to the whole screen, then the screen fades to black and the game's core is loaded. The game's own start comes out of black.

**Never in the way**: a button pressed while an animation runs takes it to its end at once and does what it does (Play during the start plays). Moving the cursor never waits for the preview. Whatever does not fit a field (a screen of rows' text) is drawn ahead, in the field the cursor stops.

## Where everything comes from

- **The list**: the launcher's catalogue (`Scripts/.danik_hybrid_ports/catalogue.txt`, every published port and game) for the ports page; for the game list, the games installed (`games/dnk/<Engine>/games.txt` there).
- **The pictures are the frontend's own**, made once and never scaled on the MiSTer: `tools/dnklauncher_art.py` makes them from every published port's repository, and the frontend brings them in `games/dnk/dnkLauncher/art/`: per game `<Engine>/<id>.shot` (the screenshot, 4:3 at 256x192: the preview and *Details* draw it the same size), `<id>.logo` (fitted into the preview's 244x64) and `<id>.dlogo` (into *Details*' 220x46), and `index.txt` with each game's title, year, developer and description. Scaled in linear light with Lanczos, dithered to RGB565 (4x4 ordered), in a file the program reads as it is ("DNKI", the size, RGB565, an alpha byte a pixel for the logos; the tool's header). 5.9 MB for the 42 games of the 12 published ports (2026-10-25). Once the frontend is published, `distribution.py` runs it at every publish (as it makes the catalogue) and puts the tree in the database as launcher files (tag `launcher`), so every player who has the launcher has them, an already published port shows its art without being packaged again, and the ports page can show ports that are not installed. Not before: while the frontend is in alpha nothing of it goes into the database, which every player's launcher installs from.
- **New assets in each core's repository**, written and taken by Claude, quality-checked by Danik (2026-10-25): descriptions in Claude's own words (others found online may inspire them, never copied), screenshots from the running games (`fakecore` on the PC, as TheForceEngine's README shot; `MISTER_HYBRID_FPS=0`), each gameplay that looks played: a moment of a level, the HUD's normal values, no cheat's traces, no message on the screen. The files: `package/screens/<id>.png` for every game of `games.txt` (gameplay, never a menu: looked at before it goes in; the picture as `fakecore`'s `shot` saves it, the size of the mode it was taken in, not stretched: the core's default mode, a 320-wide one where the game has it, which `distribution.py` scales to 4:3; a game's own level, an expansion's from the expansion, not the place the README's screenshots show), and `package/info.txt`, a line per game, `<id>|<year>|<developer>|<description>`, the description at most 4 lines of *Details* (about 340 characters). `games.txt` stays as it is (`MH_PickGame()`, `distribution.py` and older launchers read it). Required for every game, as the logos are: `distribution.py` refuses a port without them once the frontend is out, and a game without them meanwhile has the port's `docs/screenshot2.png` and no text.
- **Data missing**: today a game's files are known only to its port's C code (`MH_Game.files`, jfSW checks by checksum, `MH_NOT_THERE`). Decided (Danik, 2026-10-25): a mode of the library, `<program> --check`, which does `MH_PickGame()`'s check without the core and prints for each game whether it is there, and its message if not. The frontend runs it for every port in the background once the list is up and keeps the result until the game's folder changes (its time). A port built before it has no marks. Not the other way, the patterns written in `info.txt` too: the same thing in two places.
- **Recent**: the daemon writes every game it starts, from the frontend or its own entry, into `games/dnk/dnkLauncher/recent.txt` (set name and time; a launcher change, its `VERSION`). **Favourites** and the sort mode: `games/dnk/dnkLauncher/settings.txt`, the frontend's.

## Starting a game and coming back

1. The frontend writes `/tmp/dnk_launched` (the set name) and sends `load_core <the game's menu entry>` to `/dev/MiSTer_cmd`: the MGL of the player's layout, so the set name and the OSD settings are the game's.
2. **Shorter start**: the game core's RTL starts its intro as always; the library, at `MH_Open()`, finds the file and tells the core to end it (a bit of the control block: the MiSTer logo goes now, as at the intro's end), and `MH_GameLogo()` waits for that instead of field 300. The daemon looks for a core change every 0.5 seconds; for this it can look more often while the file is there. About 6.5 seconds from the load to the first frame instead of 9.1 (the intro cut at about 1.5, the two lines 0.8, the handover 1.5, the logo's 2 seconds, the fade-out 0.7). Leaving out the logo's 2 seconds as well would make it about 4.5; not asked for.
3. **Back**: a game that quits loads the frontend's menu entry instead of `menu.rbf` while the file names its set (`hps/mister_hybrid.c`, `load_core /media/fat/menu.rbf`), and removes the file. Quitting a game picked from a core's own list still brings that list back (exit code 42). Leaving the core from the OSD, or loading another, removes the file too (the daemon, when the core changes to one that is not the game's).
4. Both are a framework change, host version 18: a port has them once it is built again. Until then a game started from the frontend starts in full and quits to the MiSTer menu.

## Phases

1. **Mock-ups** on the PC (`tools/dnklauncher_mockup.py`, the fonts measured with `tools/dnklauncher_fonts.py`): the list, the preview, *Details*, the ports page, a game without data, *Options*, the button symbols, at 640x480 as HDMI and a 15kHz screen (interlaced) show them, and the animations. Danik decides the look and the sizes. Done: the fifth round (the list and *Details*, the animations) "looks good for a prototype" (Danik, 2026-10-25); the sizes in the tool are the program's.
2. **Assets**: `info.txt` and `screens/` for the published ports' games; the art set (`tools/dnklauncher_art.py`; into `distribution.py` when the frontend is published).
3. **The core and its program** (no framework change): `new_core.sh dnkLauncher` (no game options, `MIDI_SYNTH` 0, no HUD lines, the core without the intro), the program in C on the `MH_` library (not SDL: it draws a list), the list, sort modes, favourites, starting a game, the ports page. Recent in the daemon. Tested with `tools/test_on_mister.sh` as every core, and the ports page on a copy of an SD card (`DANIK_HYBRID_ROOT`).
4. **Host version 18**: the shorter start, the way back, `--check`. Cores take it at their next build.
5. **Documentation**: the frontend's own README and `CLAUDE.md`. Players' documentation (the README section *The launcher*, `docs/readme.md`, `OVERVIEW.md`) changes only when Danik publishes it, out of alpha.

## Open questions

None left of the first interview (2026-10-25): the 640x240 mode is fine, `--check` it is, Claude writes the descriptions and takes the screenshots for Danik to check, and the launcher stays as it is while the frontend is in alpha.
