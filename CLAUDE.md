# Hybrid cores for MiSTer: what holds for every core

This file is in `hybrid/`, the [Hybrid_MiSTer](https://github.com/ItsDanik/Hybrid_MiSTer) submodule of every hybrid core repository, and each core's `CLAUDE.md` imports it. `hybrid/README.md` is the conventions document (SD card layout, OSD order, controls, behaviour, documentation); read it before changing a core's OSD, controls, launcher, packaging or README.

## Keep the cores alike

- A new core is started with `hybrid/new_core.sh` from `hybrid/template/`. Never start one by copying another core's repository.
- Before writing something in a core, check whether `hybrid/` has it. Code a second core would need as well goes into `hybrid/` (and the template, if it is a file every core has its own copy of), not into the core.
- A change to a file that came from the template is a change for every core: make it in `hybrid/template/` too, and say which existing cores still have the old version. Lines marked `GAME:` and the `CONF_STR` are the game's own.
- `hybrid/` is its own repository (branch `main`). Commit and push there first, then update the submodule pointer in the core.

## Base of a new core

- Video starts at 320x200, the core's default mode. Higher resolutions and anything else a game can do beyond that (640x200 in ECWolf) are extra features of that core, added once the base works. They are not part of the template.
- SDL2 games build against the static SDL2 of `hybrid/sdl2` and get video, audio and input from its "mister" drivers; the game needs a `MISTER_HYBRID` build option and a small file of its own (`hybrid/template/game/`).
- One frame per field, timed by the core's field counter (`MH_WaitField()`, `MH_FieldCounter()`), not by the system clock.
- Status bits 0..23 of the OSD mean the same in every core; a game's options start at bit 24.

## Rules

- The launcher `launcher/danik_hybrid_cores.sh` is developed only here. Raise its `VERSION` with every change that reaches users and keep the contract in its header working for cores already released.
- Script names on the MiSTer keep the `danik_` prefix.
- MiSTer Frontier by MiSTer Organize is credited and thanked for the inspiration in every core's `README.md`, package `README.txt` and launch script header. Our launcher does not depend on it.
- Game data is never committed or shipped.
- Releases: `<Name>_YYYYMMDD.zip` from `./package.sh`, copied to `releases/` and committed. No git tags or GitHub releases.
