#!/bin/sh
# tools/midiapi_test.c's three tests on the PC: midiapi_test.sh <fonts>
# <fonts>: a folder with AWE64.sf2, GeneralUser-GS.sf2 and Roland SC-55.sf2
# (links will do); the test makes its own copy of the folder without slots.txt.
set -e
cd "$(dirname "$0")/.."
fonts=$(cd "$1" && pwd)
t=$(mktemp -d)
trap 'kill $fc 2>/dev/null; rm -rf "$t"' EXIT
gcc -O2 -Ihps -Itools -o "$t/fakecore" tools/fakecore.c -lpthread -lm
gcc -O2 -Wall -Ihps -o "$t/midiapi_test" tools/midiapi_test.c hps/mister_hybrid.c hps/mister_joymap.c \
    hps/mister_ui.c hps/mister_triggers.c hps/mister_games.c hps/mister_midi.c hps/mister_diag.c -lpthread -lm
mkdir "$t/slots" "$t/sf"
for f in AWE64.sf2 GeneralUser-GS.sf2 "Roland SC-55.sf2"; do ln -s "$fonts/$f" "$t/sf/$f"; done
r=0

echo "== slots"
"$t/midiapi_test" slots "$t/slots" || r=1

echo "== load"
printf 'wait 600\nsoundfont 2\nwait 3000\nquit\n' > "$t/script"
"$t/fakecore" "$t/shm" "$t/script" "$t/audio.raw" > "$t/fc.log" 2>&1 & fc=$!
sleep 0.5
MISTER_HYBRID_SHM="$t/shm" MISTER_HYBRID_SOUNDFONTS="$t/sf" "$t/midiapi_test" load 2> "$t/load.log" || { r=1; cat "$t/load.log"; }
kill $fc 2>/dev/null; wait $fc 2>/dev/null || true

echo "== dest"
printf 'wait 600\nosd2 1\nwait 3000\nquit\n' > "$t/script"
FAKECORE_MT32=1 FAKECORE_MIDILOG="$t/midi.log" "$t/fakecore" "$t/shm" "$t/script" "$t/audio.raw" > "$t/fc.log" 2>&1 & fc=$!
sleep 0.5
MISTER_HYBRID_SHM="$t/shm" MISTER_HYBRID_SOUNDFONTS="$t/sf" "$t/midiapi_test" dest "$t/midi.log" 2> "$t/dest.log" || { r=1; cat "$t/dest.log"; }
exit $r
