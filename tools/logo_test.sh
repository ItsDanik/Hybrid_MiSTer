#!/bin/sh
# tools/logo_test.c's cases against fakecore: logo_test.sh <logo.mhl>
# (tools/make_logo.py makes one). The steps of each with their fields; the
# checks at the end of each case.
set -e
cd "$(dirname "$0")/.."
t=$(mktemp -d)
trap 'kill $fc 2>/dev/null || true; rm -rf "$t"' EXIT
gcc -O2 -Ihps -Itools -o "$t/fakecore" tools/fakecore.c -lpthread -lm
gcc -O2 -Wall -Ihps -o "$t/logo_test" tools/logo_test.c hps/mister_hybrid.c hps/mister_joymap.c \
    hps/mister_ui.c hps/mister_triggers.c hps/mister_games.c hps/mister_midi.c hps/mister_diag.c -lpthread -lm
mkdir "$t/logos" "$t/data"
cp "$1" "$t/logos/doom.mhl"
touch "$t/data/DOOM.WAD"
export MISTER_HYBRID_SHM="$t/shm" MISTER_HYBRID_CORE=Logo MISTER_HYBRID_SET=Logo
r=0

# <name> <fakecore script> <logo_test arguments...>: the log in $t/<name>.log
run() {
    name=$1 script=$2
    shift 2
    printf "$script" > "$t/script"
    "$t/fakecore" "$t/shm" "$t/script" > "$t/fc.log" 2>&1 & fc=$!
    sleep 0.3
    (cd "$t/data" && "$t/logo_test" "$@") > "$t/lt.log" 2>&1 || true
    kill $fc 2>/dev/null || true; wait $fc 2>/dev/null || true
    # both logs in the order of their fields
    cat "$t/fc.log" "$t/lt.log" | awk '/field [0-9]+:/ { match($0, /field [0-9]+/); f = substr($0, RSTART + 6, RLENGTH - 6); print f "\t" $0; next } { print "\t" $0 }' \
        | awk -F'\t' 'BEGIN { last = -1 } $1 != "" { last = $1 } { printf "%08d %s\n", last, $2 }' | sort -s -k1,1n | cut -c10- > "$t/$name.log"
    echo "== $name"
    cat "$t/$name.log"
}
# the field of the first line matching $2 in log $1
at() { grep -m1 "$2" "$t/$1.log" | sed 's/.*field \([0-9]*\):.*/\1/'; }
check() { if [ "$2" -eq 1 ]; then echo "ok: $1"; else echo "WRONG: $1"; r=1; fi; }

lines_ok() {
    h=$(at "$1" "handover begins")
    # the messages' fields: the line before each message's text
    f1=$(grep -m1 "field.*message:" "$t/$1.log" | sed 's/.*field \([0-9]*\):.*/\1/')
    f2=$(grep "field.*message:" "$t/$1.log" | sed -n 2p | sed 's/.*field \([0-9]*\):.*/\1/')
    f3=$(grep "field.*message:" "$t/$1.log" | sed -n 3p | sed 's/.*field \([0-9]*\):.*/\1/')
    check "$1: the first line once the logo is in ($((f1 - h)) fields after the handover began, 92)" $([ $((f1 - h)) -ge 92 ] && [ $((f1 - h)) -le 94 ] && echo 1 || echo 0)
    check "$1: the second $((f2 - f1)) fields later, the third $((f3 - f2)) (24 each)" $([ $((f2 - f1)) -ge 24 ] && [ $((f2 - f1)) -le 26 ] && [ $((f3 - f2)) -ge 24 ] && [ $((f3 - f2)) -le 26 ] && echo 1 || echo 0)
}

run game 'wait 2000\nquit\n' game 500
lines_ok game
h=$(at game "handover begins"); g=$(at game "given up"); s=$(at game "the game has the screen"); f=$(at game "first frame$")
check "game: given up $((g - h - 92)) fields after the logo was in (120)" $([ $((g - h - 92)) -ge 120 ] && [ $((g - h - 92)) -le 123 ] && echo 1 || echo 0)
check "game: the screen $((s - g)) fields after (43), its first frame $((f - s)) after that" $([ $((s - g)) -eq 43 ] && [ $((f - s)) -le 3 ] && echo 1 || echo 0)
check "game: the lines say Doom" $(grep -q "|Loading Doom..." "$t/game.log" && echo 1 || echo 0)

run slow 'wait 2000\nquit\n' game 6000
h=$(at slow "handover begins"); l=$(at slow "loaded"); g=$(at slow "given up")
check "slow: given up when loaded, $((g - l)) fields after" $([ $((g - l)) -le 2 ] && [ $((g - h)) -gt 212 ] && echo 1 || echo 0)

run exec 'wait 2000\nquit\n' exec
lines_ok exec
h=$(at exec "handover begins"); g=$(at exec "given up")
check "exec: taken over, the handover once ($(grep -c "handover begins" "$t/exec.log")), given up $((g - h - 92)) fields after the logo was in" \
    $([ "$(grep -c "handover begins" "$t/exec.log")" -eq 1 ] && [ $((g - h - 92)) -ge 120 ] && [ $((g - h - 92)) -le 123 ] && echo 1 || echo 0)
check "exec: the program after gets the screen" $(grep -q "first frame$" "$t/exec.log" && echo 1 || echo 0)

rm "$t/data/DOOM.WAD"
MISTER_HYBRID_SET=Logo_doom run missing 'wait 400\nquit\n' missing
h=$(at missing "handover begins")
f1=$(grep -m1 "field.*message:" "$t/missing.log" | sed 's/.*field \([0-9]*\):.*/\1/')
f2=$(grep "field.*error" "$t/missing.log" | sed -n 1p | sed 's/.*field \([0-9]*\):.*/\1/')
check "missing: Checking Game Data... once the logo is in, the message $((f2 - f1)) fields after (24)" \
    $([ $((f1 - h)) -ge 92 ] && [ $((f2 - f1)) -ge 24 ] && [ $((f2 - f1)) -le 26 ] && grep -q "|Doom is not there" "$t/missing.log" && echo 1 || echo 0)
check "missing: the logo stays" $(grep -q "given up" "$t/missing.log" && echo 0 || echo 1)
touch "$t/data/DOOM.WAD"

mv "$t/logos/doom.mhl" "$t/logos/other.mhl"
run nologo 'wait 600\nquit\n' game 500
check "nologo: no overlay, Loading Doom... under the MiSTer logo" $( (grep -q "overlay" "$t/nologo.log" || ! grep -q "|Loading Doom..." "$t/nologo.log") && echo 0 || echo 1)
mv "$t/logos/other.mhl" "$t/logos/doom.mhl"

run page 'wait 2000\nquit\n' page
h=$(at page "handover begins"); m=$(grep -m1 "field.*message:" "$t/page.log" | sed 's/.*field \([0-9]*\):.*/\1/')
o=$(at page "the overlay is out"); c=$(at page "comes in again"); f=$(grep "overlay shows framebuffer" "$t/page.log" | sed -n 2p | sed 's/.*field \([0-9]*\):.*/\1/')
check "page: Loading Doom... alone, once the logo is in ($((m - h)) fields, 92)" \
    $([ "$(grep -c "field.*message:" "$t/page.log")" -eq 1 ] && grep -q "|Loading Doom..." "$t/page.log" && [ $((m - h)) -ge 92 ] && [ $((m - h)) -le 94 ] && echo 1 || echo 0)
check "page: the logo out $((o - h - 92)) fields after it was in (120 + 31)" $([ $((o - h - 92)) -ge 151 ] && [ $((o - h - 92)) -le 155 ] && echo 1 || echo 0)
check "page: its frame and the overlay in again together ($c, $f), $((c - o)) fields after the logo was out" \
    $([ -n "$c" ] && [ "$c" = "$f" ] && [ $((c - o)) -le 4 ] && grep -q "handover begins, rising" "$t/page.log" && ! grep -q "again, rising" "$t/page.log" && echo 1 || echo 0)
check "page: never the game's screen" $(grep -q "given up\|the game has the screen" "$t/page.log" && echo 0 || echo 1)

FAKECORE_VERSION=16 run v16 'wait 600\nquit\n' game 500
check "v16: no overlay, Loading Doom..." $( (grep -q "overlay" "$t/v16.log" || ! grep -q "|Loading Doom..." "$t/v16.log") && echo 0 || echo 1)
exit $r
