# The README of a port

Every port's repository has the same front page: `README.md`, for players, laid out as below and with the same words wherever this says so. What is for developers (how the port works, building, the repository, development notes) is in `NOTES.md` next to it. Decided with Danik on 2026-10-24; ECWolf's README is the example (`ItsDanik/ecwolf_MiSTer`), `template/README.md` and `template/NOTES.md` the starting point of a new core. The package's `README.txt` is short: instructions and links (see [README.txt](#readmetxt)).

## Files

| File | |
|---|---|
| `README.md` | for players, the sections below |
| `NOTES.md` | for developers, see [NOTES.md](#notesmd) |
| `docs/banner.png` | the banner at the top: `hybrid/tools/make_banner.py package/logos/<id>.png docs/banner.png`, `<id>` the first game of `package/games.txt` |
| `docs/screenshot1.png` | the title screen of that game |
| `docs/screenshot2.png` | that game being played, never a menu |

## README.md, from the top

Nothing else, and in this order. *Word for word* means the same text in every port, the core's name put in where it says `<Engine>`.

1. **The banner**, alone on the first line:
   ```html
   <p align="center"><img src="docs/banner.png" alt="<title of the first game>" width="100%"></p>
   ```
   `make_banner.py` makes it: the core's purple, the game's logo with its whole 440x200 area to itself, and under it, seamless, a stripe with "danik Hybrid Framework" and the danik icon at its right end, the badge of the core's picture. Drawn in the core's pixels and saved twice the size, nearest pixels. Made again when the logo changes.
2. **`# <Engine> for MiSTer`**
3. **The introduction**: `<Games> on [MiSTer FPGA](https://github.com/MiSTer-devel/Main_MiSTer/wiki) as a hybrid port.`, then one paragraph: which source port the game is (with its link) and that it runs on the MiSTer's ARM CPU, that the FPGA core provides native 15kHz video (CRT, VGA and HDMI) at its resolutions, 44.1kHz audio and keyboard, mouse and gamepad input, and that the two halves talk through shared DDR3 memory.
4. **The notes**, one quote, word for word:
   ```markdown
   > **Beta.** Expect rough edges and please report problems in the issues.
   >
   > **Disclaimer:** AI is being extensively used to speed up development, but the project is well-maintained and supported.
   ```
5. **Two screenshots side by side**, the title screen first, then gameplay:
   ```html
   <p align="center">
     <img src="docs/screenshot1.png" alt="<Title>, the title screen" width="49%">
     <img src="docs/screenshot2.png" alt="<Title>, <where in the game>" width="49%">
   </p>
   ```
   See [Screenshots](#screenshots).
6. **`## Requirements`**, three items. The launcher's, word for word:
   ```markdown
   - **danik_hybrid_ports**, the launcher: run it **once** from the MiSTer's `Scripts` menu ([The launcher](#the-launcher)). Without it the core says "Game not running".
   ```
   Then `- **Game data**, which is not included: ` and which copies of the games do (full games, shareware, demos). Then what to play with (Danik, 2026-10-24), word for word: in a game played with a gamepad,
   ```markdown
   - **Recommended:** a gamepad with two analog sticks and analog triggers, like an Xbox or PlayStation controller. A gamepad without them works but is not recommended. The game can also be played with a keyboard and mouse.
   ```
   ("keyboard" alone where the game does not use the mouse); in a game designed for the mouse (KeeperFX, VanillaConquer),
   ```markdown
   - **Recommended:** a mouse and a keyboard, which the game was designed for. A gamepad works as well.
   ```
   Nothing more on controllers here: a gamepad is recommended, never required, so that no player takes a game for one that cannot be played with a keyboard and mouse. A port that is no game (the MIDI Player) has neither.
7. **`## Installation`**, through `update_all` only (no "By hand", no list of the zip's files):
   ```markdown
   With `update_all` (Downloader 2.4 or newer, which update_all keeps up to date):

   1. Copy [`downloader_danik_hybrid_ports.ini`](https://raw.githubusercontent.com/ItsDanik/Hybrid_MiSTer/main/launcher/downloader_danik_hybrid_ports.ini) to the root of your SD card (`/media/fat`).
   2. Run `update_all`. That installs the launcher, **danik_hybrid_ports**, in `Scripts`.
   3. Run **danik_hybrid_ports** from the `Scripts` menu, tick the games you want and press *X: Apply*. They are installed, and update_all keeps them up to date.
   4. Copy **everything** in your game's folder to `/media/fat/games/dnk/<Engine>/` ...
   5. Load the game from the `Other` menu, e.g. **dnk-<Title>**, or from the `Hybrid Ports` folder if you moved the games there in the launcher.
   ```
   Steps 1 to 3 word for word. Step 4 is the port's: whole folders, everything in them, never files that can be left out (`CLAUDE.md`, "players copy whole folders"); with several games, a table of each game and the files it is known by. Step 5 names the first game's menu entry.

   Under the list, the USB drive as GitHub's note box, word for word but for the folder:
   ```markdown
   > [!NOTE]
   > The game data can go on a USB drive instead, into the same folder there (`/media/usb0/games/dnk/<Engine>/`): the game looks for it there first, and then keeps its settings and saved games there as well. Everything else stays on the SD card.
   ```
   Then one paragraph of what a player may want to know of the menu entries (with several games: what each starts when its data is missing, that each keeps its own OSD settings and button mappings, the core file's list of games) and where the game's log, settings and saved games are.
8. **`## The launcher`**, right after *Installation*, word for word (Danik, 2026-10-25): what the launcher does and what its screen has, with its screenshot, the same picture in every port, `docs/launcher.png` of this repository (made by `launcher/ui/screenshot.py`), by its address on GitHub:
   ```markdown
   ## The launcher

   <p align="center"><img src="https://raw.githubusercontent.com/ItsDanik/Hybrid_MiSTer/main/docs/launcher.png" alt="The launcher's screen" width="70%"></p>

   **danik_hybrid_ports** is what turns a menu entry into a running game. Once you've run it from the `Scripts` menu, it starts with the MiSTer, watches which core is loaded, and starts the game whenever a hybrid port's menu entry is loaded. It serves all our hybrid ports at once and updates itself through `update_all`.

   Run it from the `Scripts` menu again whenever you want to change something. Its screen has:

   - **Launcher**: on or off. Off, the ports' games no longer start (the core says "Game not running"), until you turn it on again.
   - **Menu entries**: where the games appear in the MiSTer menu:
     - **Other, as dnk-\<Title\>**: each game in the `Other` menu, e.g. **dnk-Doom** (the default).
     - **Other/Hybrid Ports**: all of them in a *Hybrid Ports* folder in `Other`, without the `dnk-` in front.
     - **Main menu/Hybrid Ports**: a *Hybrid Ports* folder of its own in the main menu, next to `Arcade`, `Console` and `Other`.

     Moving them keeps each game's settings and button mappings. Favourites, recent lists and Zaparoo tags that point at the old place have to be made again.
   - **The ports and their games**, a tickbox each. Tick what you want, untick what you don't.

   *X: Apply* installs what is ticked and removes what is not, through Downloader, and moves the menu entries if you chose another place. When a port is removed, your game data, saved games and settings stay, and ticking it again brings it back as it was.
   ```
   **Every launcher feature a player sees changes this section** (Danik, 2026-10-25): here, in the template and in every port's README, with `docs/launcher.png` made again, in the same change as the launcher's `VERSION`. No key or button of the screen goes into it; the screen shows them itself.
9. **`## OSD options`**, a table `| Option | |`, in the order of the OSD:
   - `| Aspect ratio, Scale, Scandoubler Fx, Stereo Mix | as in other cores |`
   - `| FPS Counter | shows the pictures per second in the top left corner |`
   - *HDMI Only*: what it does in this port (a port without 640x400 or more says nothing changes)
   - *CRT Options*, the game's options, *Cheats*, then `| Save Diagnostic Report | the last line: see [If something is wrong](#if-something-is-wrong) |`

   **An option with several settings or values** (*CRT Options*, *Resolution*, *FM Music*, *Cheats*) has a line for each, `**<Name>**: <a few words>`, `<br>` between them, the default marked `(default)`, no full stop; then, if it needs one, `<br><br>` and a short explanation of a sentence or two. A one-liner is a few words: "**Give Weapons/Ammo/Keys**: every weapon, full ammo and both keys", not when or how often it applies. *CRT Options*, its lines as the OSD has them:
   ```markdown
   | CRT Options | **Horizontal Size**: the picture's width<br>**Horizontal Pos**: moves it left or right<br>**Vertical Pos**: moves it up or down<br>**HUD (Top)**: ...<br>**HUD (Bottom)**: ...<br><br>For a 15kHz screen: fits the picture to it and keeps the HUD out of the overscan. HDMI is not affected; not there with `forced_scandoubler=1` |
   ```
   A HUD line of an edge the game has no HUD at says it is always greyed out and why; a game without a HUD has neither line.
10. **`## Controls`**: `- **Keyboard and mouse:**` and its table `| Key | Action |`, then `- **Gamepad** (default mapping):` and its table `| Button | Action |`, the buttons with their MiSTer, Xbox and PlayStation names (`B (Xbox A / PlayStation Cross)`). Under the tables only, word for word:
   ```markdown
   Change the buttons in the OSD under *Define <Engine> buttons*. *Menu OK* and *Menu Back* there are only needed for a spare button without a game function. While the OSD is open, the game gets no input.
   ```
   then GitHub's warning box, word for word but for the name and the mapping files (`config/inputs/<Engine>_input_*_v3.map` in a core with one game, `config/inputs/<Engine>_*_input_*_v3.map` (each game has its own) in a core with several):
   ```markdown
   > [!WARNING]
   > A button left without a name in *Define <Engine> buttons* does not reach the game at all, except L2/R2, L3/R3 and *Menu OK* / *Menu Back*. Name every button there, or leave the definition as it is. To go back to the default buttons, delete `config/inputs/<Engine>_input_*_v3.map` on the SD card.
   ```
   (Danik, 2026-10-24: once a core has a button definition of its own, Main_MiSTer passes it only the buttons named there; what the library reads from Linux itself still comes.) A port whose game can bind the gamepad's buttons in its own menu (ECWolf, jfSW, DXXRebirth, Xash3D, YQuake2) has after it, word for word but for the menu's path:
   ```markdown
   The game's own menu can change what each gamepad button does: *Options*, *Controls*, *Customize joystick*. Change the actions there rather than in the OSD; a button the OSD leaves without a name never reaches the game, so its menu cannot bind it.
   ```
   Then, where the game names saved games or high scores, how to type a name without a keyboard (ECWolf: "Naming a saved game without a keyboard: up/down changes the letter, left/right moves the cursor, Menu OK confirms."). Nothing else on the game's own controls menu, nothing on its keyboard keys' settings, nothing on the MiSTer menu's Menu OK / Back.
11. **`## If something is wrong`**: the diagnostic report, the template's two paragraphs (`hybrid/README.md`, "Diagnostic report").
12. **`## Building`**, one line, word for word:
    ```markdown
    How the port works, how to build it and notes for development: [NOTES.md](NOTES.md).
    ```
13. **`## Support`**, word for word:
    ```markdown
    If you enjoy this project, you can support my work on [Patreon](https://www.patreon.com/itsdanik).
    ```
14. **`## Credits`**: the game or source port and its authors; SDL; GeneralUser GS in a port with MIDI (`MIDI_SYNTH` 1); MiSTer; and MiSTer Frontier, word for word:
    ```markdown
    - **[MiSTer Frontier](https://github.com/MiSTerOrganize/MiSTer_Frontier)** by MiSTer Organize: thank you for the inspiration. Hybrid cores on the MiSTer, and the way their game is launched (a daemon that watches the loaded core and runs a script from its games folder), were inspired by MiSTer Frontier.
    ```
    Then `This project and its maintainers are in no way associated with or endorsed by <the rights holders>. It does not include any game data.`
15. **`## License`**: the template's paragraph, with the game's licence; `hybrid/hps/`, which the game is linked with, named as GPL-2.0-or-later.

**When a core is first built**, its README is the last step, after testing and packaging: show it to Danik (rendered, as GitHub shows it) and ask him whatever is unsure (what a setting really does, which screenshot, what a player must know), so that the documentation is right from the start.

A short note a player needs that has no slot of its own may stay where it belongs: a row in a controls table (a racing wheel, an action without a default button), a line under the OSD table (a game option that does nothing on the MiSTer). Anything longer goes to `NOTES.md`.

Sections a port had beyond these (ECWolf's *Frame pacing*, *Music*, KeeperFX's *What is different on the MiSTer*) go to `NOTES.md`; what a player needs of them goes into the options' or the controls' lines in a few words.

## Screenshots

- Two: `docs/screenshot1.png` the title screen of the first game of `package/games.txt`, `docs/screenshot2.png` that game being played: the view of a level, a race, a map, with its HUD, never a menu, a list or a loading screen. Look at both before they go in (ECWolf's episode menu was once taken for the first level).
- In the core's default resolution (320x200 or 320x240), saved twice the size with nearest pixels: 640x400 or 640x480. The MIDI Player, whose picture is 640x480, as it is.
- Made on the PC: the game built for it (`./build.sh host`) against `hybrid/tools/fakecore`, its data copied to a scratch folder, `MISTER_HYBRID_CORE=<Engine>` and `MISTER_HYBRID_SET=<Engine>_<id>` (a core with one game: `<Engine>`), `HOME` a scratch folder too. The script waits for the intro (700 fields), presses its way into the game and takes `shot <file.ppm>` along the way. A shot is 640x480 with the picture of the video mode at its top left: crop that (320x200) and double it.

## NOTES.md

```markdown
# <Engine> for MiSTer: notes

How the port works and how it is built. For playing it, see the [README](README.md).
```

Then, in this order: the sections on how the port works that are its own (ECWolf: *Frame pacing*), or none; `## Building` (the commands, the toolchain); `### Repository layout`; `### Development notes`. These are the README's former sections, moved as they were.

## README.txt

`package/games/<Engine>/README.txt`, in the zip and on the SD card in `games/dnk/<Engine>/`, next to the player's game files; the database's README links to it. Plain text, lines of 76 characters at most, and only this (ECWolf's is the example):

```
<Engine> for MiSTer
===================                     (as long as the title)

<Games> on MiSTer FPGA as a hybrid port.

Everything about it (options, controls, help):
  https://github.com/ItsDanik/<repository>

Game files
  Copy everything in your game's folder into this folder,
  /media/fat/games/dnk/<Engine>/, all the files as they are:
    <the games and the files each is known by, as the README's step 4>
  They can go on a USB drive instead, in /media/usb0/games/dnk/<Engine>/.

Playing
  1. Run danik_hybrid_ports once from the MiSTer's Scripts menu. It starts
     the game whenever its menu entry is loaded.
  2. Load the game from the Other menu, e.g. dnk-<Title>, or from the
     Hybrid Ports folder if you moved the games there in the launcher.
  Buttons are changed in the OSD under "Define <Engine> buttons". A button
  left without a name there does not reach the game at all: name every
  button, or leave the definition as it is.

If something is wrong
  Open the OSD while it happens and pick Save Diagnostic Report, the last
  line. Send the file it saves in games/dnk/diagnostics/ with a few words
  on what went wrong: https://github.com/ItsDanik/<repository>/issues

Credits and licenses
  <the game or source port and its authors>: <licence> (<its LICENSE files
  in the zip>). SDL by Sam Lantinga and contributors: zlib
  (LICENSE-sdl.txt). MiSTer by Sorgelig and the MiSTer-devel contributors.
  Inspired by MiSTer Frontier by MiSTer Organize. The launcher: GPL-3.0
  (LICENSE-gpl3.txt); the FPGA core: GPL-2.0. Source:
  https://github.com/ItsDanik/<repository>

Support the project: https://www.patreon.com/itsdanik
```

The licence files named are the ones `package.sh` copies into the zip; GeneralUser GS and its licence in a port with MIDI; nothing on options, controls or how the port works, which the GitHub README has. A port whose data is more than copying a folder (KeeperFX) says in *Game files* what goes where, as short as it can be said.
