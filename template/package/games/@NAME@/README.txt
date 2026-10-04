@NAME@ for MiSTer - @TITLE@ as a hybrid core
====================================================

The game (TODO which source port) runs on the MiSTer's ARM CPU; the @NAME@
FPGA core provides native 15kHz video (CRT, VGA and HDMI) at 320x200, audio
and input.

Requirements
  - danik_hybrid_cores, the launcher that comes with this release
    (Scripts/danik_hybrid_cores.sh): copy it to /media/fat/Scripts/ and run
    it ONCE from the MiSTer's Scripts menu. It starts the game whenever
    the core is loaded, keeps running after a reboot, and serves all our
    hybrid cores. Every hybrid core brings the launcher along and the
    newest version is the one that runs, so it never has to be run again
    after an update. Without it the core only shows colour bars.
  - Game data. It is not included: TODO which copy of the game.

Install
  1. Copy _Other/@NAME@_*.rbf to /media/fat/_Other/
  2. Copy games/@NAME@/ to /media/fat/games/@NAME@/
  3. Copy the game data to /media/fat/games/@NAME@/
       TODO which files, where
  4. Run danik_hybrid_cores from the Scripts menu, if you have not done so
     before (see Requirements).
  5. Load @NAME@ from the Other menu.

OSD options
  Aspect ratio, Scale, Scandoubler Fx, Stereo Mix as in other cores.
  TODO game options
  Menu OK, Menu Back
                the gamepad button that confirms / goes back in the game's
                menus, whatever it does in the game. MiSTer (default) uses
                the OK/Back buttons of your MiSTer menu.

Controls
  Keyboard and mouse:
    TODO
  Gamepad (default mapping):
    Left stick                          TODO
    D-pad                               TODO
    A (Xbox B / PlayStation Circle)     TODO
    B (Xbox A / PlayStation Cross)      TODO
    X (Xbox Y / PlayStation Triangle)   TODO
    Y (Xbox X / PlayStation Square)     TODO
    L (LB / L1)                         TODO
    R (RB / R1)                         TODO
    Select                              TODO
    Start                               Menu
  Change the buttons in the OSD under "Define @NAME@ buttons". "Menu OK" and
  "Menu Back" there are only needed for a button without a game function;
  MiSTer doesn't let you assign a button twice, so to confirm with a button
  that also has a game function, pick it in the OSD's Menu OK/Menu Back
  options instead.

Files
  TODO settings and saved games
  /media/fat/logs/@NAME@/@name@.log   the game's log

Credits
  TODO the game / source port by its authors.
  SDL by Sam Lantinga and contributors.
  MiSTer by Sorgelig and the MiSTer-devel contributors.
  MiSTer Frontier by MiSTer Organize
  (https://github.com/MiSTerOrganize/MiSTer_Frontier): thank you for the
  inspiration. Hybrid cores on the MiSTer, and the way their game is
  launched, come from MiSTer Frontier. Our launcher is a separate
  implementation and does not need MiSTer Frontier installed.

License
  TODO the game: its license (LICENSE-@name@.txt)
  SDL: zlib (LICENSE-sdl.txt)
  The launcher scripts: GPL-3.0 (LICENSE-gpl3.txt)
  The FPGA core: GPL-2.0
  Source: https://github.com/ItsDanik/@REPO@
          https://github.com/ItsDanik/Hybrid_MiSTer (launcher, framework)
