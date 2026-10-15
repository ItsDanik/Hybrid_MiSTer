#!/bin/sh
# The intro of the picture without a game as hybrid_host puts it out
# (sim/tb_wall.sv), to an animated GIF of 640x480, a frame for each frame of
# 480i (30 a second; 480p: every field, with VGA31=1 in the environment),
# before any core is built. Pieces of it are simulated side by side.
# usage: sim/wall_gif.sh <out.gif> [<first field> <last field>]   (0 270)
#   the path as seen from the framework's directory; the fields are those of
#   the intro (hybrid_host.sv, "Picture without a game"), 242 its end
cd "$(dirname "$0")/.."
OUT=$1
FROM=${2:-0}
TO=${3:-270}
VGA31=${VGA31:-0}
[ -n "$OUT" ] || { echo "usage: sim/wall_gif.sh <out.gif> [<first field> <last field>]" >&2; exit 1; }
docker image inspect mister-hybrid-sim > /dev/null 2>&1 || docker build -t mister-hybrid-sim toolchain/sim
WORK=$(mktemp -d "$PWD/.wall_gif.XXXXXX")
PER=$([ "$VGA31" = 1 ] && echo 1 || echo 2)   # fields per frame
STEP=$((16 * PER))                            # fields per piece
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src mister-hybrid-sim \
  iverilog -g2012 -o "/src/${WORK#$PWD/}/tbw" -Ptb_wall.VGA31="$VGA31" -Ptb_wall.START=0 -Ptb_wall.FRAMES=1 \
  sim/tb_wall.sv rtl/hybrid_host.sv rtl/hybrid_fm.sv sim/jtopl2_stub.v || exit 1
s=$FROM
while [ "$s" -le "$TO" ]; do
  # the start and the number of frames are run-time arguments of a build per piece
  docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src mister-hybrid-sim sh -c "
    iverilog -g2012 -o /tmp/tbw -Ptb_wall.VGA31=$VGA31 -Ptb_wall.START=$s -Ptb_wall.FRAMES=$((STEP / PER)) \
      -Ptb_wall.OUT=\\\"/src/${WORK#$PWD/}/f$(printf %04d "$s")\\\" \
      sim/tb_wall.sv rtl/hybrid_host.sv rtl/hybrid_fm.sv sim/jtopl2_stub.v && vvp -n /tmp/tbw" &
  s=$((s + STEP))
done
wait
python3 - "$WORK" "$OUT" "$PER" <<'END'
import glob, sys
from PIL import Image
work, out, per = sys.argv[1], sys.argv[2], int(sys.argv[3])
frames = [Image.open(f).convert("RGB") for f in sorted(glob.glob(work + "/f*.ppm"))]
# a GIF counts in hundredths of a second: each frame ends where the real time is
ends = [round(100 * per * (n + 1) / 59.64) * 10 for n in range(len(frames))]
durations = [b - a for a, b in zip([0] + ends, ends)]
frames[0].save(out, save_all=True, append_images=frames[1:], duration=durations, loop=0, optimize=False)
print("%s: %d frames" % (out, len(frames)))
END
rm -rf "$WORK"
