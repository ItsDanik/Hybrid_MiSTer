# @NAME@ hybrid core for MiSTer (@TITLE@)

@hybrid/CLAUDE.md

## This core

- Name everywhere: `@NAME@` (the engine: core, folders `games/dnk/@NAME@/` and `logs/dnk/@NAME@/`, `_Other/dnk/dnk-@NAME@_*.rbf`, release), `@name@` (game submodule, binary in `build/`, log, lock). The games and their menu entries (`_Other/dnk-<title>.mgl`): `package/games.txt` (in the zip as `games/dnk/@NAME@/games.txt`, from which the database's build makes the launcher's catalogue).
- The game is the `@name@/` submodule, branch `mister`. Its MiSTer code is behind the `MISTER_HYBRID` build option.
- Build: `./build.sh` (game), `./core/build_core.sh` (FPGA), `./package.sh` (zip in `dist/`), `MISTER=root@<address> tools/deploy.sh` (its menu entries in `_Test`, the main menu's *Test*).
- Release: the zip in `releases/`. Players get it once it is published to the database, only when Danik asks: `hybrid/launcher/distribution.py publish ~/dnk_Distribution_MiSTer releases/@NAME@_YYYYMMDD.zip` (the rule in `hybrid/CLAUDE.md`).
- Try a build on the MiSTer without installing it: `MISTER=root@<address> tools/test_on_mister.sh <id>|list|nodata|log|stop` (the rule in `hybrid/CLAUDE.md`).
- Lines marked `GAME:` in the scripts and `TODO` in the READMEs are the places the template left for this game.

<!-- Notes that are only true for this game go below. -->
