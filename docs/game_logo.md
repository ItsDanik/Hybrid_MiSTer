# The game's logo: every port starts the same way (host version 17)

Danik's idea, 2026-10-22; the answers below are Danik's of the same day. It is part of host version 17, with the MIDI Player's pieces (`docs/midiplayer.md`). CrispyDoom is the first core built against it (2026-10-22).

## What the player sees

1. The purple screen with its intro, as now (4.1 seconds).
2. The game is known: the menu entry's, or picked from the list. **A core started by itself with several games shows the list first, under the MiSTer logo, and the game's logo comes after the pick.**
3. The handover (the overlay's, `docs/midiplayer.md`, "The screen"):
   - the MiSTer logo goes as it came, its intro step backwards: fading out while it sinks 16 pixels, 31 fields (0.5 s); the dots and any message go with it;
   - 30 fields (0.5 s) of the background and the badge alone;
   - **the game's logo** comes as the MiSTer logo came: it fades in while it rises 16 pixels into its place, on the same curve, 31 fields (0.5 s). Its bottom edge ends where the MiSTer logo's was (Danik, 2026-10-22).
4. Under the logo, where messages are, the lines pile up, each shown for at least 0.4 seconds before the next comes:
   ```
   Checking Game Data...
   Game Data OK
   Loading Doom...
   ```
   The title is the game's of `package/games.txt`.
   - **Data missing:** the logo stays, and "Checking Game Data..." is replaced by the message there is now, naming the files and where they go (the rule "missing game files are said on the purple screen"). It waits until the player leaves the core.
5. **The logo stays at least 2 seconds from the moment it is fully faded in**, even if the game is ready sooner. The game loads meanwhile.
6. The fade-out there is now (0.7 s to black, logo and lines with everything else), then the game's first frame.

From the core's start to the game's first frame that is about 8.3 seconds at the least, instead of 4.8 now.

## The logo files

- **One logo per game**, keyed by the game's id in `package/games.txt`: `package/logos/<id>.png` in the core's repository. A core with one game has one.
- **440 x 200 pixels** (Danik, 2026-10-22; first 429 x 111, the MiSTer logo's size). Centred across the screen (x 100), its bottom edge on the MiSTer logo's (row 295 of 480, so its top at row 95), never scaled. A logo lower than 200 pixels leaves the top of the PNG transparent.
- A PNG, transparency allowed. The overlay knows only "transparent" (palette index 0) and "solid". So `tools/make_logo.py` blends a half-transparent pixel onto the purple background (7F30A0) when the core is packaged, and it makes fully transparent pixels index 0. At most 255 colours after that: more are reduced, with a warning naming the logo.
- **Required from host version 17.** `package.sh` (the template's) refuses to package a core without a logo of that size for every game of `games.txt`. `new_core.sh` names the files still to be drawn.
- **Shipping:** `package.sh` converts each logo with `make_logo.py` into `games/dnk/<Engine>/logos/<id>.mhl`. That file holds the palette and the 8bpp pixels, about 89 KB. It stays on the SD card next to the program (`GAMEDIR`), as the libraries do; the library finds it next to its own program (`/proc/self/exe`), the name in any case. A shareware or demo version (no line in `games.txt`) shows its full game's logo.
- `make_logo.py <logo.png> out.mhl --preview out.png --title <title>` shows it as the core will: on the purple screen with the badge and the three lines, for checking a logo before a build (as `make_wall.py --preview` does for messages).

## Framework changes

### RTL (`hybrid_host.sv`)

- The overlay as written for the MIDI Player: `w0[58]`, the handover in three steps, its level put into its palette as the palette is read. While it fades in, it is drawn up to 16 rows lower (`WALL_RISE`, the MiSTer logo's eased curve), the rows above it transparent. The game's logo is an overlay frame: transparent but for the logo's rectangle, so the lines show through under it.
- **New: from the overlay to the game.** The overlay asked for, then given up while the magic stays (`w0[58]` cleared): the picture goes out as it does now for a game, the overlay with it, and then the game gets the screen. The overlay's colours are put at the master level too, as the picture's are. The MiSTer logo stays gone: today it would come back for those 0.7 seconds.
- A game built against host version 16 or older never sets `w0[58]`, and starts as now: no logo, no lines.

### Library

As built (`hps/mister_hybrid.c`, `hps/mister_games.c`):

- `MH_PickGame()` does it all on a core of host version 17: its data check (the `files` patterns) is done before it picks, so the game is never kept waiting. With the game it returns it calls `MH_GameLogo(id, title)` (the title is the game's `name`), which
  1. draws the logo into a framebuffer the core does not show, its colours into the palette slot it does not use, and asks for the overlay (`w0[58]`, mode 640x480, 8bpp),
  2. starts a thread that waits for the handover (status bit 29: the logo is fully in 92 fields later, `OVL_T_IN + OVL_D_IN`), clears the message there (the list goes out with the MiSTer logo), and shows the lines at 0, 24 and 48 fields from the logo fully in,
  3. returns, and the game loads.

  A game needs no code. The template's `MiSTer_PickGame()` lost its own "Loading <title>..." message, which is the third line now. A port with its own pick code calls `MH_GameLogo(id, title)` once it knows the game, and `MH_GameLogoMissing(id)` before the message naming missing files (`MH_PickGame()` does that for a menu entry: the logo comes in, "Checking Game Data..." for 24 fields, then the message, which waits).
- **The first frame waits:** a game's first `MH_Present()` or `MH_PresentBuffer()` waits until the three lines are there and the logo has been in for 120 fields (2 seconds). Then the overlay is given up with the game's mode but still read at 8bpp (the core's picture goes out with it, 43 fields), and once the core gives the game the screen the game's palette, format and frame follow. `MH_SetPalette()`, `MH_SetMode()` and `MH_SetFormat()` before that only note what the game wants and return at once: loading is never held up by them.
- A message of the game's own meanwhile (`MiSTer_ShowError()`) takes the lines' place under the logo, and they stop.
- **A program started in its place** (`execv()` after `MH_Close()`: CrispyDoom's Heretic, Hexen and Strife): `MH_Close()` leaves the overlay on and puts the logo's state in `MISTER_HYBRID_LOGO`; the next `MH_Open()` takes it over (the lines and the 2 seconds go on where they were). A program that just ends with the logo up leaves it until the next one opens the core: `mh_message` of the launch script then turns it off.
- No logo file, or a core of host version 16 or older: no overlay, "Loading <title>..." under the MiSTer logo, as before. A release cannot lack one (`package.sh`); this is for a test build. No handover within 15 seconds of asking for it (a core that does not begin it): the same, "Loading <title>..." alone.
- The FPS counter is drawn into the game's frames only; the diagnostic report's screenshot is taken only while the game has the screen.

### The MIDI Player

It is a port like the others: its logo and the 2 seconds, with "Loading MIDI Player..." alone under it (a game without files to check: `MH_GameLogoLoading()`, which `MH_PickGame()` uses for one; Danik, 2026-10-22). Its own frame then takes the overlay's place, the purple screen staying, with no fade to black (Danik, 2026-10-22, "Logo out, then page in"): with `MH_Overlay(1)` the first frame asks the overlay out (control word 2 bit 58: its fade-in backwards, the logo sinking and the line fading with it, 31 fields), waits for it (status bit 30), clears the line, puts the page's palette into the other slot, and its frame brings the overlay in again, fading in without rising (bit 59, the rise, is the logo's only). `tools/logo_test.sh` checks it (`page`), `sim/logo_gif.sh` with a page of mode P shows it.

## Rules and documents to change

- `CLAUDE.md`, "every port starts the same way": the logo, the lines, the 2 seconds; and the logo is required from host version 17.
- `hybrid/README.md`, the start of a game and the messages; each core's README if it describes the start.
- The template: `package.sh` (logo check and conversion), `new_core.sh`, `game/mister_port.c`, `package/logos/`.
- `tools/make_wall.py`: the badge says v17.

## Tests

- `sim/logo_gif.sh <out.gif> <logo.mhl> <frame.ppm>` (`sim/tb_wall.sv` with `LOGO`): the whole start at 480i, from the core's black fields to the game's first frame. Danik approved it with the Doom logo (2026-10-22).
- `sim/tb_host.sv`: a game of host version 16 (no `w0[58]`) starts as before, pixel for pixel; the overlay given up goes to the game.
- fakecore (`tools/logo_test.sh <logo.mhl>`, `tools/logo_test.c`; fakecore knows the overlay's handover and fade): the lines' fields, the 2 seconds from the logo fully in, a load longer than that, the `execv()` takeover, a missing file's message under the logo, a game without a logo file, a core of host version 16, the MIDI Player's page. All 18 checks pass (2026-10-22).
- On the MiSTer (after asking), with `tools/test_on_mister.sh`:
  - every game of a core with several (CrispyDoom), from its menu entry and from the list;
  - `nodata`;
  - a game whose data is missing;
  - 480i on the CRT and HDMI.
- Each core built again with host version 17 is checked with its logos on the MiSTer before it is released.
