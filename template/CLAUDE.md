# @NAME@ hybrid core for MiSTer (@TITLE@)

@hybrid/CLAUDE.md

## This core

- Name everywhere: `@NAME@` (core, folders, release), `@name@` (game submodule, binary in `build/`, log, lock).
- The game is the `@name@/` submodule, branch `mister`. Its MiSTer code is behind the `MISTER_HYBRID` build option.
- Build: `./build.sh` (game), `./core/build_core.sh` (FPGA), `./package.sh` (zip in `dist/`), `MISTER=root@<address> tools/deploy.sh`.
- Lines marked `GAME:` in the scripts and `TODO` in the READMEs are the places the template left for this game.

<!-- Notes that are only true for this game go below. -->
