# @NAME@ hybrid core for MiSTer (@TITLE@)

@hybrid/CLAUDE.md

## This core

- Name everywhere: `@NAME@` (the engine: core, folders `games/dnk/@NAME@/` and `logs/dnk/@NAME@/`, `_Other/dnk/dnk-@NAME@_*.rbf`, release), `@name@` (game submodule, binary in `build/`, log, lock). The games and their menu entries (`_Other/dnk-<title>.mgl`): `package/games.txt`, the same as in the catalogue of `hybrid/launcher/danik_hybrid_ports.sh`.
- The game is the `@name@/` submodule, branch `mister`. Its MiSTer code is behind the `MISTER_HYBRID` build option.
- Build: `./build.sh` (game), `./core/build_core.sh` (FPGA), `./package.sh` (zip in `dist/`), `MISTER=root@<address> tools/deploy.sh`.
- Lines marked `GAME:` in the scripts and `TODO` in the READMEs are the places the template left for this game.

<!-- Notes that are only true for this game go below. -->
