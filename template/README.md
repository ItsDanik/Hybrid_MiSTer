<p align="center"><img src="docs/banner.png" alt="@TITLE@" width="100%"></p>

# @NAME@ for MiSTer

@TITLE@ on [MiSTer FPGA](https://github.com/MiSTer-devel/Main_MiSTer/wiki) as a hybrid port.

TODO: one paragraph: which source port the game is and where it comes from. It runs on the MiSTer's ARM CPU. The @NAME@ FPGA core provides native 15kHz video (CRT, VGA and HDMI) at 320x200, 44.1kHz audio and keyboard, mouse and gamepad input. The two halves talk through shared DDR3 memory.

> **Beta.** Expect rough edges and please report problems in the issues.
>
> **Disclaimer:** AI is being extensively used to speed up development, but the project is well-maintained and supported.

<p align="center">
  <img src="docs/screenshot1.png" alt="@TITLE@, the title screen" width="49%">
  <img src="docs/screenshot2.png" alt="@TITLE@, TODO where in the game" width="49%">
</p>

## Requirements

- **danik_hybrid_ports**, the launcher: run it **once** from the MiSTer's `Scripts` menu ([The launcher](#the-launcher)). Without it the core says "Game not running".
- **Game data**, which is not included: TODO which copy of the game.
- **Recommended:** a gamepad with two analog sticks and analog triggers, like an Xbox or PlayStation controller. A gamepad without them works but is not recommended. The game can also be played with a keyboard and mouse.
  TODO: or, in a game designed for the mouse: **Recommended:** a mouse and a keyboard, which the game was designed for. A gamepad works as well.

## Installation

With `update_all` (Downloader 2.4 or newer, which update_all keeps up to date):

1. Copy [`downloader_danik_hybrid_ports.ini`](https://raw.githubusercontent.com/ItsDanik/Hybrid_MiSTer/main/launcher/downloader_danik_hybrid_ports.ini) to the root of your SD card (`/media/fat`).
2. Run `update_all`. That installs the launcher, **danik_hybrid_ports**, in `Scripts`.
3. Run **danik_hybrid_ports** from the `Scripts` menu, tick the games you want and press *X: Apply*. They are installed, and update_all keeps them up to date.
4. Copy **everything** in your game's folder to `/media/fat/games/dnk/@NAME@/`: TODO which folders, copied whole, all the files as they are (never files that can be left out: `CLAUDE.md`, "players copy whole folders"). With several games, a table of each game and the files it is known by.
5. Load the game from the `Other` menu, e.g. **dnk-@TITLE@**, or from the `Hybrid Ports` folder if you moved the games there in the launcher.

> [!NOTE]
> The game data can go on a USB drive instead, into the same folder there (`/media/usb0/games/dnk/@NAME@/`): the game looks for it there first, and then keeps its settings and saved games there as well. Everything else stays on the SD card.

The game's log is in `/media/fat/logs/dnk/@NAME@/@name@.log`. TODO: where settings and saved games are.

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

## OSD options

| Option | |
|---|---|
| Aspect ratio, Scale, Scandoubler Fx, Stereo Mix | as in other cores |
| FPS Counter | shows the pictures per second in the top left corner |
| HDMI Only | **Yes** if no CRT is connected: resolutions of 640x400 and more are then progressive instead of interlaced, and so is the core's own screen before the game, which moves at 60 pictures a second for a CRT and shows fine lines on HDMI without it. The analog output is switched off while one is shown, so that a 15kHz screen can never get such a signal. Nothing changes at lower resolutions. Not needed with `forced_scandoubler=1` (VGA monitor); not there with `direct_video=1` |
| CRT Options | **Horizontal Size**: the picture's width<br>**Horizontal Pos**: moves it left or right<br>**Vertical Pos**: moves it up or down<br>**HUD (Top)**: TODO<br>**HUD (Bottom)**: TODO<br>**Flicker Filter**: Off (default), Light or Strong: softens fine lines that blink while the picture is interlaced<br><br>For a 15kHz screen: fits the picture to it and keeps the HUD out of the overscan. HDMI is not affected but by the Flicker Filter; not there with `forced_scandoubler=1` |
| TODO game options | one-liners: `**<Name>**: <a few words>`, `<br>` between them, the default marked; then `<br><br>` and a short explanation if needed (`hybrid/docs/readme.md`) |
| Cheats | TODO, as the game options |
| Reset | starts the game again from its beginning, as the USER button does |
| Save Diagnostic Report | the last line: see [If something is wrong](#if-something-is-wrong) |

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

Change the buttons in the OSD under *Define @NAME@ buttons*. *Menu OK* and *Menu Back* there are only needed for a spare button without a game function. While the OSD is open, the game gets no input.

> [!WARNING]
> A button left without a name in *Define @NAME@ buttons* does not reach the game at all, except L2/R2, L3/R3 and *Menu OK* / *Menu Back*. Name every button there, or leave the definition as it is. To go back to the default buttons, delete `config/inputs/@NAME@_input_*_v3.map` on the SD card.

TODO, where the game's own menu can bind the gamepad's buttons, else delete: The game's own menu can change what each gamepad button does: TODO *its menu's path*. Change the actions there rather than in the OSD; a button the OSD leaves without a name never reaches the game, so its menu cannot bind it.

TODO, where the game names saved games or high scores, else delete: how to type a name without a keyboard (ECWolf: "Naming a saved game without a keyboard: up/down changes the letter, left/right moves the cursor, Menu OK confirms.").

## If something is wrong

If the game stutters, the sound breaks up or something else is wrong, open the OSD while it happens and pick **Save Diagnostic Report**, the last line of the menu. The game keeps running; MiSTer's info box says the report is saved in `games/dnk/diagnostics/` on the SD card. Send that file (`@NAME@_<date>_<time>.zip`) with a few words on what went wrong.

The report has the setup (versions, the video and audio lines of `MiSTer.ini`, the OSD settings), how the last 60 seconds went, the game's log and a screenshot. Nothing else: no network settings, addresses, saved games or lists of your files.

## Building

How the port works, how to build it and notes for development: [NOTES.md](NOTES.md).

## Support

If you enjoy this project, you can support my work on [Patreon](https://www.patreon.com/itsdanik).

## Credits

- **TODO the game / source port** by its authors.
- **[SDL](https://libsdl.org)** by Sam Lantinga and contributors.
- TODO with MIDI music (`MIDI_SYNTH` 1), else delete: **[GeneralUser GS](https://www.schristiancollins.com/generaluser.php)** by S. Christian Collins: the SoundFont of the core's MIDI synthesizer, which comes with the port, its licence next to it.
- **[MiSTer](https://github.com/MiSTer-devel)** by Sorgelig and the MiSTer-devel contributors: the framework and Template_MiSTer.
- **[MiSTer Frontier](https://github.com/MiSTerOrganize/MiSTer_Frontier)** by MiSTer Organize: thank you for the inspiration. Hybrid cores on the MiSTer, and the way their game is launched (a daemon that watches the loaded core and runs a script from its games folder), were inspired by MiSTer Frontier.

This project and its maintainers are in no way associated with or endorsed by TODO the rights holders. It does not include any game data.

## License

The top-level scripts, tools and documentation are licensed under the [GPL-3.0](LICENSE). The components keep their own licenses: the part of the shared framework the game is linked with (`hybrid/hps/`) is GPL-2.0-or-later, the rest of `hybrid/` GPL-3.0 except where its files say otherwise, TODO the game's license, SDL and the SDL drivers in `hybrid/sdl2` are zlib, the FPGA core and the MiSTer framework are GPL-2.0 (`core/LICENSE`, with the core's own sources GPL-2.0-or-later).
