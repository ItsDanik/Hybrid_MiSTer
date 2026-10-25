@NAME@ for MiSTer
=================

@TITLE@ on MiSTer FPGA as a hybrid port.

Everything about it (options, controls, help):
  https://github.com/ItsDanik/@REPO@

Game files
  Copy everything in your game's folder into this folder,
  /media/fat/games/dnk/@NAME@/, all the files as they are:
    TODO the games and the files each is known by (hybrid/docs/readme.md)
  They can go on a USB drive instead, in /media/usb0/games/dnk/@NAME@/.

Playing
  1. Run danik_hybrid_ports once from the MiSTer's Scripts menu. It starts
     the game whenever its menu entry is loaded.
  2. Load the game from the Other menu, e.g. dnk-@TITLE@, or from the
     Hybrid Ports folder if you moved the games there in the launcher.
  Buttons are changed in the OSD under "Define @NAME@ buttons". A button
  left without a name there does not reach the game at all: name every
  button, or leave the definition as it is.

If something is wrong
  Open the OSD while it happens and pick Save Diagnostic Report, the last
  line. Send the file it saves in games/dnk/diagnostics/ with a few words
  on what went wrong: https://github.com/ItsDanik/@REPO@/issues

Credits and licenses
  TODO the game or source port and its authors: TODO licence (TODO its
  LICENSE files in the zip). SDL by Sam Lantinga and contributors: zlib
  (LICENSE-sdl.txt). MiSTer by Sorgelig and the MiSTer-devel contributors.
  Inspired by MiSTer Frontier by MiSTer Organize. The launcher: GPL-3.0
  (LICENSE-gpl3.txt); the FPGA core: GPL-2.0. Source:
  https://github.com/ItsDanik/@REPO@

Support the project: https://www.patreon.com/itsdanik
