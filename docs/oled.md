# The OLED display: tty2oled+

[tty2oled+](https://github.com/ItsDanik/MiSTer-tty2oled-plus) (Danik's fork of tty2oled) drives an SSD1322 OLED panel, 256x64 pixels in 16 levels of grey, that is plugged into the MiSTer over USB. It shows what is played: a picture of the game, then its title, details and description beside an icon. For a hybrid port it shows the game that runs, from files the port brings along. A MiSTer without tty2oled+ gets no tty2oled+ folder: the files are in the port's own folder, and tty2oled+ looks for them there (Danik, 2026-10-26).

## What a port ships

For every game of `package/games.txt`, in `games/dnk/<Engine>/` (`package.sh`, with `tools/make_oled.py package`):

| file | what |
|---|---|
| `oled/<id>_logo.gsc` | the game's logo, 256x64: the whole panel, shown when the game starts |
| `oled/<id>_icon.gsc` | the game's icon, 86x64: beside its title and details |
| `info.txt` | a line per game, `<id>\|<year>\|<developer>\|<description>`: `package/info.txt` in printable ASCII (accents dropped), which is all the display draws |

A `.gsc` is tty2oled's picture: three lines of header (`#define icon_width 256`, `#define icon_height 64`, `static unsigned char icon_bits[] = {`) and then one hex digit a pixel, row by row, 0 black to f full brightness. Only the `.gsc` files are shipped; the PNGs they are made from stay in the repository. A shareware or demo version has no line of `games.txt` and shows its full game's.

## In the repository

| file | what |
|---|---|
| `package/oled/<id>_icon.png` | **every game has one**: 86x64 in the panel's 16 levels (0, 17, 34 ... 255), from Danik or drafted by Claude and approved by him |
| `package/oled/<id>_logo.png` | the logo as Danik approved it, 256x64 in the 16 levels: made from `package/logos/<id>.png` (below), or composed, or his own. Until then the logo is made from `package/logos/<id>.png` when packaged |
| `package/info.txt` | the line of each game (the one dnkLauncher uses, `docs/dnklauncher.md`) |

`package.sh` refuses a game without its icon, its logo or its line of `info.txt`. Each game of a core with several has its own files, also where they show the same picture (an add-on with no art of its own takes its base game's).

**The logo made from the game's logo.** `make_oled.py logo package/logos/<id>.png <out>` crops the 440x200 logo to what is not transparent, puts it on black, turns it grey, stretches its contrast, lifts the dark parts a little (a logo drawn for colour is dim in grey), fits it into 256x64 a pixel off every edge, and reduces it to the 16 levels. A logo of one line comes out well; a logo of two or three lines (Wolfenstein 3-D, Quake, The Need for Speed) comes out small, and is where Danik's own `package/oled/<id>_logo.png` helps. A picture of 256x64 (86x64 for an icon) already in the 16 levels is taken as it is, so a picture drawn for the panel is never changed. Once Danik approves a logo it is saved as `package/oled/<id>_logo.png`, so that a later change to the tool never changes what he approved.

**Margins** (Danik, 2026-10-26): a logo keeps at least 1 pixel clear of every edge of the panel, an icon 2 pixels of every edge of its 86x64; some keep more where he asked (2 off the top and bottom for a tall logo). `make_oled.py` does it by default (`--margin n` or `--margin h,v` for another), fitting a picture to what of it is not transparent or not black. A subtitle that comes out unreadable is better composed than scaled: Scourge of Armagon's logo is the Quake rune with the add-on's name to its right, the main game's title left out.

**Pictures that tty2oled+ shipped.** Before 2026-10-26 tty2oled+ had a banner and an icon of its own for Dethrace, ECWolf, jfSW and openscreamer (`pics/banner`, `pics/icon`). They are the logos and icons of CARMA, WL, SW and S1 now (`make_oled.py gsc2png`, nothing lost: a `.gsc` has the same 16 levels), and tty2oled+ no longer has them.

## Making them

- **Before a game is packaged for the first time, ask Danik for its icon** (and a logo for the panel, if he wants one other than the one made from its logo), as PNGs of any size, or whether Claude should draft the icon. A draft is a single thing from the game's own art, bright on black, filling most of the 64 rows, readable at that size in grey, no words: a HUD face (B.J.'s, Max Damage), an emblem (Shadow Warrior's yin-yang, Quake's), a key monster or item. Pixel art is scaled by whole numbers. `make_oled.py icon <src.png> <out.png> [--crop x,y,w,h] [--margin n]` fits a picture into 86x64, two pixels off its edges, and reduces it to the levels. An add-on whose art is not its own takes its base game's icon (Twin Dragon and Wanton Destruction show Shadow Warrior's, The Reckoning and Ground Zero Quake II's).
- **Danik approves every logo and icon** on the review page of the packaged core's games (CLAUDE.md, "every packaged core's games are reviewed by Danik"): each card shows the logo and the icon as the panel shows them (`make_oled.py preview <package dir> <out.png>` draws them). What he sends back is drawn again or replaced with his PNG.
- `tools/dnklauncher_art.py` lists a game without its icon among what is missing.

## How tty2oled+ finds the game

Main_MiSTer writes the name of what was loaded to `/tmp/CORENAME`: the set name of a game's menu entry (`ECWolf_SOD`), or the engine's name when the core was started by itself. That does not say which game was picked from the core's list, or which game `tools/test_on_mister.sh` tries (its set name is `<Engine>_TEST`), so the library writes `/tmp/dnk_game`, `<Engine>|<id>`, once the game is known (`MH_GameLogo()`, `MH_GameLogoLoading()`, which `MH_PickGame()` calls). It is never removed. tty2oled+ takes, in this order:

1. `/tmp/dnk_game` when it is newer than `/tmp/CORENAME` and names the engine loaded (`/tmp/CORENAME` is `<Engine>` or starts with `<Engine>_`);
2. `/tmp/CORENAME` as `<Engine>_<id>`, where `games/dnk/<Engine>/oled/<id>_logo.gsc` is there;
3. `/tmp/CORENAME` as `<Engine>`, where `games/dnk/<Engine>/games.txt` has a single game: that one.

The engine's folder is `/media/fat/games/dnk/<Engine>/` (the programs and these files stay on the SD card when the game's data is on a USB drive). The game's title is its line of `games.txt`; the rows shown are *Dev* (the developer), *Year*, *Port* (the engine) and *Author* (danik), and the description is the last page. A core started by itself whose list is up shows nothing of a game until one is picked; a port packaged before this has no `oled/` and shows the core's name as any core does.

Cores built before 2026-10-26 do not write `/tmp/dnk_game`: their games show from a game's menu entry, and from the list only in a core with one game. They write it once built again.
