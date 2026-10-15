@NAME@ for MiSTer - @TITLE@ as a hybrid port
====================================================

The game (TODO which source port) runs on the MiSTer's ARM CPU; the @NAME@
FPGA core provides native 15kHz video (CRT, VGA and HDMI) at 320x200, audio
and input.

Disclaimer: AI is being used to speed up development of this project.

Requirements
  - danik_hybrid_ports, the launcher that comes with this release
    (Scripts/danik_hybrid_ports.sh): run it ONCE from the MiSTer's Scripts
    menu. It starts the game whenever its menu entry is loaded, keeps
    running after a reboot, and serves all our hybrid ports; its screen
    picks which ports and games to have, and can keep them up to date
    through update_all. It replaces danik_hybrid_cores, the launcher before
    it, and moves ports installed the old way into the new folders by
    itself. Without it the core says "Game not running".
  - Game data. It is not included: TODO which copy of the game.

Install
  1. Extract the zip to the root of the SD card (/media/fat): _Other/,
     games/dnk/@NAME@/ and Scripts/.
  2. Copy the game data to /media/fat/games/dnk/@NAME@/
       TODO which files, where
  3. Run danik_hybrid_ports from the Scripts menu, if you have not done so
     before (see Requirements).
  4. Load dnk-@TITLE@ from the Other menu.

OSD options
  Aspect ratio, Scale, Scandoubler Fx, Stereo Mix as in other cores.
  FPS Counter   shows the pictures per second in the top left corner.
  HDMI Only     Yes if no CRT is connected: resolutions of 640x400 and
                more are then progressive instead of interlaced, and the
                analog output is switched off while one is shown, so that a
                15kHz screen can never get such a signal. Nothing changes
                at lower resolutions. Not needed with forced_scandoubler=1
                (VGA monitor); not there with direct_video=1.
  CRT Options   for a 15kHz screen: Horizontal Size, Horizontal Pos and
                Vertical Pos fit the picture to it. The pixels stay as they
                are; HDMI is not affected. Not there with
                forced_scandoubler=1.
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
  /media/fat/logs/dnk/@NAME@/@name@.log   the game's log

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
