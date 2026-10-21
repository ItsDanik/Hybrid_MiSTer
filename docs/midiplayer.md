# MIDI Player: a core that plays MIDI files and manages the SoundFonts

Danik's idea, 2026-10-21. The interview and its mockups are at https://claude.ai/artifact/DC6dbQnHFwncTsMQQCfiqr (rounds 1 to 4, the decisions numbered there). The work starts after the MIDI synthesizer (`docs/midi_synth.md`) is finished. The slots file (below) is part of host version 16. Everything else needs host version 17.

## What it is

- A core of its own. It plays `.mid` files, and the music inside the installed ports' games, on the core's synthesizer with any SoundFont, or on an MT32-pi when one is connected.
- It is also the SoundFont manager. Each font of `games/dnk/SoundFonts/` can be heard and checked: its size, and whether it fits the 64 MB. The player chooses which fonts go into the 8 slots of the *SoundFont* line that every core's OSD has.
- Names (agreed with Danik): the engine and its folders are `MIDIPlayer`, and players read "MIDI Player".
  - SD card: `_Other/dnk-MIDI Player.mgl`, `_Other/dnk/dnk-MIDIPlayer_YYYYMMDD.rbf`, `games/dnk/MIDIPlayer/`, `logs/dnk/MIDIPlayer/`.
  - The set name is `MIDIPlayer`, and the repository is `MiSTer-MIDIPlayer`, started with `new_core.sh`.
  - `package/games.txt` has one line, the player itself.
- It is not a port: the program is ours, written in C against the `MH_` library (no SDL2). It sends its notes through `MH_MidiEvent()` exactly as a game does.

## The screen: the purple screen stays

- The core's picture stays, with its background, the badge at the bottom right and the messages. The player's frame is drawn over it.
- **The handover, in two steps** (Danik, 2026-10-21; mockups L, replacing G's crossfade). Once the intro has ended:
  1. The logo goes as it came: the intro's logo step backwards, fading out evenly while it sinks the 16 pixels it rose, on the intro's curve reversed. That takes 31 fields (0.5 s). The dots under "MiSTer" fade with it. The badge (icon and text) stays as it is.
  2. 30 fields (0.5 s) of the background and the badge alone.
  3. The player's frame fades in evenly over 31 fields (0.5 s).
- The player starts drawing when it likes. The core shows nothing of the frame until step 3.
- That needs a new host feature, the **overlay** (host version 17). Other cores can use it for their menus too.
  - The game's frame is 640x480 at 8 bits per pixel. Palette index 0 is transparent: the core's picture shows through it.
  - The core keeps drawing its picture under the frame (`game_on` stays 0 for the picture's own timing). The purple screen still ignores *Horizontal Size*.
  - `MH_Overlay(1)` starts it. The fades are the core's own work (`WALL_T_*` / `WALL_D_*` of the steps above): the logo's level and offset, then the pause, then the overlay's level.
  - The mode is the purple screen's own 640x480. At 15kHz it is interlaced, as that screen is now (`hybrid/README.md`, "Video modes").
- The player keeps clear of the badge (x >= 520, y >= 380), and the button rows end before it.
- The mouse cursor is the player's own, drawn into its frame. It is shown while the mouse has moved in the last 3 seconds.
- Font: the core's Cozette 6x13 (`rtl/hybrid_wall_font.hex`), the rows 16 pixels apart.
- Colours:
  - background 7F30A0 (index 0, the core's)
  - ink EFEFEF
  - dim C4A4DC
  - faint 9C6CC0
  - selection 5A1E7A
  - highlight FFD86A (what plays, the keys)

## Layout (A: two panes; mockups E, F, I, J, K)

- The top row has "MIDI Player" on the left and the tabs *Player* and *SoundFonts* on the right, with a line under them.
- The left pane is 250 pixels wide and lists the folder: its path above it, the selection a filled row, the song playing in highlight with `>`, and lengths on the right.
- The right pane, from x 316, shows what plays:
  - the title (highlight), and where it comes from (dim);
  - the file's text events (below);
  - the time, a progress bar and the length;
  - where it plays and with which font;
  - play order and repeat;
  - the 16 channels: number, mute or solo mark, instrument, and level.
- Under the panes are two rows of buttons, each a key and what it does. A lit button is a setting that is on: *repeat all*, *shuffle*. Their labels change with what the key would do (*A pause* or *A play*).

## The Player page

### Songs

- At the top of the list:
  - **My songs**: `games/dnk/MIDIPlayer/`, or a USB drive's (the `DATADIR` rule, `hybrid/README.md`, "Naming and layout"), with its folders. `.mid`, `.midi`, `.kar`, and `.mus` (Doom's) files.
  - **From the installed ports**: one folder per game whose data is there, the music read in place from its file (the port's own data folder, SD card or USB):
    - CrispyDoom: Doom, Doom II, Final Doom (TNT, Plutonia), Heretic, Hexen and Strife. The `D_*` / `MUS_*` lumps are MUS, turned into MIDI in the player.
    - jfSW: Shadow Warrior's `.MID` files in `SW.GRP`.
    - The other ports have no MIDI music: ECWolf's is OPL (IMF), Dethrace, YQuake2 and Xash3D use CD audio, OGG or MP3.
  - A game that is not installed is not listed.
- A port's songs are named:
  - Doom and Doom II by map and title ("E1M1 At Doom's Gate"), from a table in the player;
  - Hexen by map and the map's name from `MAPINFO`;
  - the others by their lump or file name.
- The lengths are computed when a folder is opened: the tempo map read through, no sound.
- Nothing plays at the start. If neither folder has a song, the list says where songs go: "Copy .mid files to games/dnk/MIDIPlayer/".

### Playing

- Double-click or A on a song plays it. A plays or pauses the song that is playing, whichever song is selected. Start moves the list to the song playing.
- Play order: *folder in order* or *shuffle* (Y). Repeat (X) cycles through *off*, *one* and *all*. With repeat off, the end of the last song stops the music: that song stays selected, its time back at 0:00.
- The song keeps playing while the player moves through other folders or the SoundFont page.
- Seeking: a click on the progress bar, or Left / Right on the keyboard (5 s).
  - The player goes through the song from the start without notes, sending each channel's last program, controllers, pitch bend and tempo, then plays on from there. Nothing sounds from the notes it skipped.
- Mute and solo:
  - A click on a channel mutes it, and a right-click solos it. More than one channel can be solo.
  - A muted channel gets *all notes off* at once and no note-ons after.
  - The marks: `M` on faint, `S` on highlight. They last until another song starts.
- The channel's instrument:
  - on the synthesizer, the preset's name in the SoundFont (bank and program, drums on channel 10);
  - on the MT32-pi, the General MIDI name.
  - The level is the loudest note sounding, by velocity, volume and expression, falling off over a quarter of a second. It is at rest while paused.
- The file's text events: a meta event 1, 2 or 3 (text, copyright, track name) of the first track before the first note, up to 3 lines, cut to the pane. No lyrics and no keyboard view in the first version.

### Where it plays

- With no MT32-pi: "On: Synthesizer - <font>".
- With an MT32-pi connected there is a switch, *Synthesizer* | *MT32-pi* (a click, or Select). The player keeps its choice.
- The Pi's own SoundFont, synth and ROM stay on the core's OSD lines (bits 96..104), and the pane shows "Pi: SoundFont n" or "Pi: MT-32".
- The player's choice decides where the music goes, not the OSD's MT32-pi line. The library's dest is the player's: a new `MH_MidiDest()`, host version 17.
- `MH_MidiSynthChanged()` makes the player send the song's settings again, as a game does.

## The SoundFonts page (mockup K)

- On the left are the 8 slots: *Default* and 1 to 7, each with its font or *(empty)*. Under them: "Every core's SoundFont line shows these; stored in games/dnk/SoundFonts/slots.txt".
- On the right are the fonts of `games/dnk/SoundFonts/` (`MH_SoundFontDir()`), each with its slot.
  - A font over 64 MB of samples is faint and says *too big*; one that is no SoundFont 2 says *damaged*. Neither can be chosen.
  - The selected font's file name, sample size and number of presets are shown under the list.
- **Choosing a font (A, or a double-click) loads it, and the song playing goes on with it.** That is the preview. The player keeps that font as its own and remembers it.
  - While the font loads (seconds for a big one), the pane says "Loading <font>...", and the music is faded out meanwhile, as when a game's slot changes.
  - Loading a named file needs `MH_MidiLoadSoundFont(path)`, host version 17.
- *Put in a slot* (A on a font, then the slot chosen on the left, as in mockup H) and *clear slot* (X on a slot).
  - Any slot can be set, *Default* included. A font can be in more than one slot.
  - Each change writes `slots.txt` at once.
- Every core reads the slots when its game loads a font, so a change reaches a game the next time it is started.
- If the folder has no font at all, the right pane says "Copy GeneralUser-GS.sf2 to games/dnk/SoundFonts/". The player can still play on an MT32-pi.

### `slots.txt` (host version 16)

```
# SoundFont slots of the OSD, written by MIDI Player
0 GeneralUser-GS.sf2
1 Arachno SoundFont - Version 1.0.sf2
4 SC-55 Presets.sf2
```

- In the SoundFont folder: `games/dnk/SoundFonts/slots.txt`, on the drive `MH_SoundFontDir()` found.
- Each line is a slot number (0..7), one space, and a file of that folder. Lines starting with `#` and empty lines are skipped.
- A slot that is not listed, or whose file is not there, is empty, and an empty slot plays slot 0's font, as now. If slot 0 is missing too, the old rule decides: `GeneralUser-GS.sf2`, else the first `.sf2` by name.
- Without the file the slots are as before: 0 `GeneralUser-GS.sf2`, N the Nth other `.sf2` by name. A player who never starts MIDI Player sees no change.
- The library reads it in `soundfont_of()`. The player writes it with `MH_SoundFontSlots()` / `MH_SetSoundFontSlot()` (host version 17; until then by hand).

## Controls

| | Pad | Keyboard | Mouse |
|---|---|---|---|
| Move | d-pad, stick | arrows, PgUp / PgDn, Home / End, a letter jumps to the next name with it | wheel scrolls |
| Select | | | click |
| Play / open | A (*Menu OK*) | Enter | double-click |
| Back up a folder | B (*Menu Back*) | Backspace | `..`, or the *B back* button |
| Pause / play | A on the song playing | Space | *A pause* button |
| Previous / next song | L / R | `,` / `.` | buttons |
| Repeat | X | `R` | button |
| Shuffle | Y | `S` | button |
| Go to the song playing | Start | `G` | button |
| Switch page | Select | Tab | the tabs |
| Synthesizer / MT32-pi | Select on the Player page, with a Pi | `P` | the switch |
| Seek | | Left / Right (5 s) while the song playing is selected | click on the progress bar |
| Mute / solo a channel | | `1`..`9`, `0` (10), with Shift solo; channels 11..16 by mouse | click / right-click |

- *Menu OK* and *Menu Back* are the MiSTer menu's (SDL buttons 28 / 29 and the core's `J1` list; the rule "menus work with Menu OK and Menu Back"). The pad keys above are the core's defaults, in its `J1` list.
- Every key that goes down comes up, and nothing is read while the OSD menu is open (the library does it).
- The mouse is `MH_ReadInput()`'s, which Main_MiSTer gives the core (USB or PS/2).

## What it remembers

`games/dnk/MIDIPlayer/midiplayer.cfg` (in `DATADIR`) keeps:
- the folder and the song selected (not playing);
- the font;
- the play order and the repeat;
- where it plays (synthesizer or MT32-pi).

Mutes and solos are not kept. It is written when one of them changes, not every frame.

## OSD

- The template's lines: *HDMI Only* (bit 1), *Stereo Mix*, *FPS Counter* (bit 15), the *CRT Options* page (bits 64..76).
- The MT32-pi's lines (96..104, shown while one is connected).
- No *HUD* lines (no HUD), no *Music* / *SoundFont* lines (the page does that), and no *Cheats* page (the rule asks for cheats; a player has none).
- The `J1` list: A, B, X, Y, L, R, Select, Start, *Menu OK*, *Menu Back*.

## Starting and messages

- As every port: the purple screen with its intro, then "Loading MIDI Player..." until the first frame. The handover above replaces the fade to black. The message goes with the logo, in step 1.
- No SoundFont and no MT32-pi: the player starts anyway (its SoundFonts page says what is missing), and the song list says on the Player page that nothing can play. That is the rule for missing files, given in the player itself because it stays usable: songs can be browsed, and an MT32-pi can be plugged in.
- A song that cannot be read (not a MIDI file, or damaged) gets one line in the right pane ("Not a MIDI file"), and the next song plays.
- Messages that wait (`MH_UI_WAIT`) fit the 4 lines of 71.

## Host version 17: what the framework gets

- **Overlay**: the RTL (the core's picture under the game's 8bpp frame, index 0 transparent, the logo's fade against the overlay's) and `MH_Overlay()`. Tested in `sim/tb_host.sv` and with `sim/wall_shot.sh` (pictures of the fade).
- `MH_MidiLoadSoundFont(path)`: a font by file, not by slot.
- `MH_MidiDest(dest)`: the synthesizer or the Pi chosen by the program rather than the OSD.
- `MH_SoundFontSlots()` / `MH_SetSoundFontSlot()`: read and write `slots.txt`.
- The template gets none of these lines: a core uses them when it needs them.

## Tests

- fakecore: the overlay (a frame with index 0 holes), the slots file (each case of the rules above), a font loaded by name.
- On the MiSTer with `tools/test_on_mister.sh`:
  - every page, with the pad, the keyboard and the mouse;
  - *Menu OK* / *Menu Back* on buttons that also have functions;
  - the stick and the d-pad;
  - an MT32-pi switched in and out (by a tester; Danik has none);
  - a font over 64 MB and a damaged one;
  - `TEST_USB=1` with songs and fonts on a USB drive;
  - `nodata` (no songs, no fonts).
- After a slot change, a game started on CrispyDoom plays with that font.
