# Diagnostic report: specification

A player who has a problem with a port (it stutters, the sound breaks up, the picture is wrong) picks one line in the OSD, and the port saves a zip file with what we would otherwise ask them: the setup, the versions, the settings and how the last minute of play went. The player sends the file; nothing has to be asked back and forth.

## Decisions (Danik, 2026-10-21)

1. **Started from the OSD while the game runs**: *Save Diagnostic Report*, the last line of the OSD's main page, in every core. It works at the moment the player notices the problem.
2. **The last 60 seconds of play** are in it: the library keeps a rolling record all the time and the line saves it, so the report shows what happened just before.
3. **On the SD card**, in `games/dnk/diagnostics/`, and **Main_MiSTer's info box** says it was saved and where (as the SoundFont errors are told). The game keeps running.
4. **Only what bears on the problem**: never network or Wi-Fi settings, addresses, saved games, or lists of the player's files.
5. **A screenshot** of the moment is in it.
6. **Host version 16**, with the synthesizer: every port rebuilt for it has the report.
7. **The record is always on** (2026-10-21, after the benchmark below): no OSD switch to turn diagnostics on first, so a problem that happened once can be reported right after. An opt-in line (*Diagnostics: Off / On* on bit 17, *Save Diagnostic Report* greyed out while off) was the alternative if the record had cost frames.

## What the player does

Open the OSD, pick *Save Diagnostic Report* (the last line), close the OSD. The info box says *Diagnostic report saved in games/dnk/diagnostics/* (or that it could not be saved, and why: no space, the folder cannot be written). The file is `games/dnk/diagnostics/<Engine>_<YYYYMMDD>_<HHMMSS>.zip`; the player takes it from the SD card (or over the network share) and sends it with a description of the problem.

A port's README says this in a short section, *If something is wrong*, the same in every port (the template's README).

## What is in the zip

| File | What |
|---|---|
| `summary.txt` | One page to read first: the port and its game, the versions, the video mode and output, the 60 seconds in a few numbers (frames shown twice, never shown, the worst lateness, CPU use), anything that stands out (another program using the CPU, the game's data on a USB drive, a full SD card, the audio ring running dry). |
| `pacing.csv` | The last 60 seconds, a line a second: fields, frames shown, frames shown twice, frames never shown, the latest a frame came in its field (ms), the game thread's CPU %, the helper threads' CPU %, the whole system's CPU %, audio ring underruns, the music's path (FM chip, synthesizer, MT32-pi). |
| `setup.txt` | The versions: Main_MiSTer, the core's build date and host version, the port's release, the library's build; the video and audio lines of the ini Main_MiSTer reads (`MiSTer.ini`, or the alternative `MiSTer_*.ini` the player picked: its number in the HPS memory Main_MiSTer keeps, `altcfg()`, never the others) that apply to this core (its own section too): `video_mode`, `vsync_adjust`, `vscale_mode`, `direct_video`, `forced_scandoubler`, `composite_sync`, `ypbpr`, `vga_scaler`, `vga_sog`, `dvi_mode`, `hdmi_limited`, `hdmi_audio_96k`, `refresh_min` / `refresh_max`, `vrr_mode`, `video_info`, `fb_size` and the like (the list is in the library; others are left out); the mode the core shows and its measured clock; where the game's data is (SD card or USB, `MH_GamePath()`), the free space there. |
| `osd.txt` | The port's OSD settings decoded: the framework's lines (CRT Options, HUD margins, Stereo Mix, FPS Counter, Music, SoundFont, the MT32-pi's) by name, the game's own bits in hex. |
| `system.txt` | The kernel, uptime, load, free memory, the CPU's clock; the processes using the most CPU (a script left running is a common cause); the controllers' names (`/proc/bus/input/devices`, names and vendor / product ids only); USB drives and how they are mounted. |
| `log.txt` | The game's log of this session (what `danik_hybrid_launch.sh` writes to `logs/dnk/<Engine>/`), its end if it is long. |
| `screenshot.png` | The frame on the screen when the line was picked, at the game's resolution (the OSD is not in it; none while the core's own picture is there). |
| `game/<name>` | What the game added with `MH_DiagAdd()`, if anything. |

Left out on purpose: `wpa_supplicant.conf` and every other network file, IP and MAC addresses, the rest of `MiSTer.ini`, saved games, the game's data, listings of folders.

## How it works

- **The OSD line**: `"T[16],Save Diagnostic Report;"`, the last line of the template's `CONF_STR` (before `J1`, which is no menu line). Bit 16 is free since host version 14 (it was *Menu OK*); a trigger's pulse is too short for the game to see, so the core flips the bit at each pulse, as it does for bits 56..59 (`hybrid_host.sv`). Every core built again with host version 16 takes the line from the template.
- **The record** (`hps/mister_diag.c`): the library already measures each frame's field for `MISTER_HYBRID_PACE`; those counts are now kept all the time. A thread of the library (on CPU1, nice 10) keeps 60 one-second slots, with the CPU times of the game thread and of the whole process (`/proc/self/task/<tid>/stat`, `/proc/self/stat`) and of CPU0 and CPU1 (`/proc/stat`) once a second. Nothing is written until the line is picked.
- **The report**: the same thread looks at bit 16 ten times a second, so a report can be made while the game hangs, and with no game code at all. It copies the frame on the screen and the record at once, then gathers the rest (a second of it to see which processes use the CPU) and writes the zip, stored, not compressed (about 250 KB at 320x240), as `.part` renamed when complete. No program is needed on the MiSTer.
- **The screenshot** is the library's copy of the frame the core shows (8bpp with its palette, or RGB565), as a PNG it writes itself, not Main_MiSTer's `screenshot` command: that one saves into the player's `screenshots/` folder and puts its own info box up (`screenshot_info`), over ours.
- **The info box**: `MH_OSDInfo()` with two new fixed texts in the template's `"I,"` line, 17 *Diagnostic report saved / in games/dnk/diagnostics/* and 18 *Diagnostic report not saved / (no space or the folder / cannot be written)* (`MH_INFO_DIAG_SAVED`, `MH_INFO_DIAG_FAILED`). A fixed text cannot hold the file's name; the folder is enough.
- **A port** needs nothing of its own beyond the template's lines. A port that knows more about its own state (the game's settings file, the map being played) may add a file with `MH_DiagAdd(name, text)`, read when the report is made; that is optional.
- **For us**: `tools/diag_read.py <zip>` prints `summary.txt` and a small graph of the 60 seconds, and compares two reports.
- **For tests**: `MISTER_HYBRID_DIAG_ROOT=<dir>` stands for `/media/fat` (`MiSTer.ini`, Main_MiSTer's program, the report's folder); `MISTER_HYBRID_DIAG=0` turns the record off.

## Tests

- `fakecore`: a script line `diag` flips bit 16; the zip is checked for its files, `pacing.csv` against the frames `fakecore` saw, and that no excluded file is in it (a `MiSTer.ini` with a Wi-Fi line and an IP address in it, which must not appear).
- On the MiSTer with `tools/test_on_mister.sh` (asked for first): a report from a game running at 640x240 with the music on the synthesizer, and one with something else using the CPU (a busy loop on CPU0), which the report must show; the info box; a full or read-only folder gives the second message and does not stop the game.
- The frame pacing with the record on, against host version 16 without it: no change (the benchmark of `CLAUDE.md`).

## Done on the PC (2026-10-21)

- With `fakecore` (`diag`) and a test program that presents a frame every field and is slow once every 2 seconds, 76 seconds: the report's 60 seconds have 30 frames never shown and 30 shown twice, 3,548 frames in 3,578 fields, as the program made them; `diag_read.py` reads it. A `MiSTer.ini` with Wi-Fi keys, a network share's address and a section of another core: none of it in the report, the core's own section and `[video=...]` in it. A folder that cannot be written: text 18, and the game ran on to its end. The library compiles without warnings for the PC and the ARM.
- `testbench` (`sim/tb_host.sv`, `+trigonly`): one pulse of bit 16 flips the bit once; the bit held at 1 (an old saved setting) does not flip it again; its end with the next pulse flips it back.

## Done on the MiSTer (2026-10-21)

Scratch CrispyDoom with host version 16, Plutonia, `tools/test_on_mister.sh`:
- At 640x240 with the music on the synthesizer: the line picked while demo1 ran, the zip (167 KB, 7 files) saved and read by `diag_read.py`: 3,546 frames in 3,578 fields, 33 never shown, 65 shown twice, the game thread 86% of CPU0. The screenshot right. It had all four of the player's ini files in it; now only the one Main_MiSTer reads (tested on the PC with alternatives 0, 2 and 3).
- With a busy loop on CPU0 from second 20: "something besides the game thread kept CPU0 13% busy", the loop in the process list (16%).
- A folder that cannot be written: not saved, the reason in the log, the game ran on to the demo's end. The info box itself was not seen (no OSD in screenshots).
- Benchmark, Plutonia demo1 at 640x200 with MIDI, 12,300 frames a run, frames never shown + shown twice: record on 19, 7, 2; off (`MISTER_HYBRID_DIAG=0`) 0, 4, 14, in turns; phase 4 without the record 13, 8, 3. The spread from run to run is larger than any difference. The record's thread on the PC: under 10ms of CPU in 41 seconds.
- Found on the way: Main_MiSTer counts a core's `"I,"` line as a line of the OSD's main page when Enter is pressed but does not draw it, so a line below it runs the one above it (the scratch core had it above *Cheats*). The template keeps it last and says so.

## Open

- Main_MiSTer itself used 68 to 70% of CPU1 while the game ran (the report's process list); not looked into.
- The `MiSTer.ini` keys are `ini_keys` in `mister_diag.c`, taken from Main_MiSTer's `cfg.cpp` (2026-10-20): those of video, audio and input.
- How many reports to keep: all for now (each a few hundred KB, made only when a player asks).
