#!/bin/bash
#
# @NAME@ (@TITLE@) launcher. danik_hybrid_cores (Scripts/danik_hybrid_cores.sh)
# runs it when the @NAME@ core is loaded and stops it, with SIGTERM to the
# process group, when another core is loaded.
#
# Laid out after the per-core handlers of MiSTer Frontier by MiSTer Organize
# (https://github.com/MiSTerOrganize/MiSTer_Frontier, GPL-3.0), with thanks
# for the inspiration. Licensed under the GPL-3.0.

CORE="@NAME@"
GAMEDIR="/media/fat/games/$CORE"
LOGDIR="/media/fat/logs/$CORE"
LOG="$LOGDIR/@name@.log"

cd "$GAMEDIR" || exit 1
mkdir -p "$LOGDIR" config saves

# Only once
exec 9> /tmp/@name@.lock
flock -n 9 || exit 0

# Development: keep the core loaded without starting the game
[ -f /tmp/@name@_nolaunch ] && exit 0

# FPGA settle after the core was just loaded
sleep 1

mv -f "$LOG" "$LOGDIR/@name@.prev.log" 2>/dev/null

# For the hybrid core drivers in the binary: which core to attach to and its
# default button mapping ("jn" in core/@NAME@.sv)
export MISTER_HYBRID_CORE="$CORE"
export MISTER_HYBRID_JN="A,B,X,Y,L,R,Select,Start"
# Everything the game writes stays in its folder
export HOME="$GAMEDIR"
export XDG_CONFIG_HOME="$GAMEDIR/config"
export XDG_DATA_HOME="$GAMEDIR/config"

# Both CPUs: the game takes CPU0 (better DDR3 bandwidth, Main_MiSTer lives on
# CPU1) and runs audio and the frame copy on CPU1. Everything else on the
# MiSTer (scripts, daemons, interrupts) lands on CPU0 as well: without the
# higher priority the game waits for it a quarter of the time and misses fields.
# Exit code 42: the player quit a game picked from a list (MH_UI_Menu), start
# again to show the list. Exit code 43: the core was loaded again while the
# game ran (MH_CoreReloaded), start again once it has settled, unless it was
# another core after all.
GAME=
# The game must not outlive us: on SIGTERM it saves its settings and quits
trap '[ -n "$GAME" ] && kill "$GAME" 2>/dev/null; exit 0' TERM INT
while :; do
    # GAME: its command line (where it finds data, settings and saved games)
    nice -n -20 taskset 0x03 ./@NAME@ >> "$LOG" 2>&1 &
    GAME=$!
    wait "$GAME"
    RC=$?
    if [ $RC -eq 43 ]; then
        sleep 1
        grep -q "^$CORE" /tmp/CORENAME 2>/dev/null || break
    elif [ $RC -ne 42 ]; then
        break
    fi
done
GAME=

# Back to the MiSTer menu, unless another core was loaded meanwhile
if grep -q "^$CORE" /tmp/CORENAME 2>/dev/null; then
    echo "load_core /media/fat/menu.rbf" > /dev/MiSTer_cmd
fi
