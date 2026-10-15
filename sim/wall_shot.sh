#!/bin/sh
# The picture without a game as hybrid_host puts it out once its intro is
# over (sim/tb_wall.sv), to a PNG, before any core is built.
# usage: sim/wall_shot.sh <out.png> [<dir with the hybrid_wall*.hex files,
#        default rtl>] [1: 480p, as with vga31]
#   paths as seen from the framework's directory (put the files under it);
#   tools/make_wall.py --out <dir> makes another picture's files to try.
#   MESSAGE="line|line|..." in the environment: with that message (as a game
#   puts it, error: ERROR=1; a list with line n selected: LIST=n),
#   MESSAGE=timeout: with the core's own.
#   START=<field of the intro>: from there instead of the end (242: the end
#   at once)
cd "$(dirname "$0")/.."
OUT=$1
DIR=${2:-rtl}
VGA31=${3:-0}
[ -n "$OUT" ] || { echo "usage: sim/wall_shot.sh <out.png> [<dir>] [1]" >&2; exit 1; }
docker image inspect mister-hybrid-sim > /dev/null 2>&1 || docker build -t mister-hybrid-sim toolchain/sim
MSGFILE=
TIMEOUT=0
if [ "$MESSAGE" = timeout ]; then
  TIMEOUT=1
elif [ -n "$MESSAGE" ]; then
  MSGFILE=$(mktemp "$PWD/.wall_msg.XXXXXX")
  python3 -c "
import sys
lines = sys.argv[1].split('|')[:4]
text = b''.join(l.encode()[:80].ljust(80, b'\\0') for l in lines + [''] * (4 - len(lines)))
words = [0x534D484D | 1 << 32 | int(sys.argv[2] or 0) << 48]
if sys.argv[4]: words[0] |= (2 | int(sys.argv[4]) << 2) << 48
words += [int.from_bytes(text[n:n + 8], 'little') for n in range(0, 320, 8)]
open(sys.argv[3], 'w').write(''.join('%016X\\n' % w for w in words))" "$MESSAGE" "$ERROR" "$MSGFILE" "$LIST"
fi
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src mister-hybrid-sim sh -c '
  iverilog -g2012 -o /tmp/tbw -Ptb_wall.WALL=\"$1/hybrid_wall.hex\" -Ptb_wall.WALL_PAL=\"$1/hybrid_wall_pal.hex\" \
    -Ptb_wall.WALL_DOTS=\"$1/hybrid_wall_dots.hex\" -Ptb_wall.WALL_EASE=\"$1/hybrid_wall_ease.hex\" \
    -Ptb_wall.OUT=\"/tmp/wall\" -Ptb_wall.VGA31=$2 -Ptb_wall.MSG=\"$3\" -Ptb_wall.TIMEOUT=$4 -Ptb_wall.START=$5 \
    sim/tb_wall.sv rtl/hybrid_host.sv rtl/hybrid_fm.sv sim/jtopl2_stub.v &&
  vvp -n /tmp/tbw >&2 && cat /tmp/wall_000.ppm' sh "$DIR" "$VGA31" "${MSGFILE#$PWD/}" "$TIMEOUT" "${START:--1}" > "$OUT.ppm" || { rm -f "$OUT.ppm" "$MSGFILE"; exit 1; }
rm -f "$MSGFILE"
python3 -c "
import sys
from PIL import Image
Image.open(sys.argv[1] + '.ppm').save(sys.argv[1])" "$OUT"
rm -f "$OUT.ppm"
