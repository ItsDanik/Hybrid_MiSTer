#!/bin/sh
# A game's start with its logo (host version 17, docs/game_logo.md) as
# hybrid_host puts it out (sim/tb_wall.sv with LOGO), to an animated GIF of
# 640x480 at 480i, 30 frames a second: from the core's start (its black
# fields, the intro) through the handover, the game's logo, the lines and the
# fade-out to the game's first frame, held for a second. Pieces of it are
# simulated side by side.
# usage: sim/logo_gif.sh <out.gif> <logo.mhl> <game first frame.ppm | page.png>
#   <logo.mhl>  from tools/make_logo.py
#   the frame   320x200, as a PPM (P6) with at most 256 colours; or a page of
#               the overlay (the MIDI Player's), 640x480 with a palette (PNG
#               of mode P, index 0 transparent): the logo goes out after its
#               2 seconds and the page comes in (tb_wall's PAGE)
#   FIRST=<field> LAST=<field>: only that part of the timeline (0, 576)
#   paths as seen from the framework's directory
# a slot of this PC's simulations, hybrid/tools/jobs.py shows them
[ -n "$DNK_JOB" ] || exec "$(dirname "$0")/../tools/job.sh" sim "$(basename "$0" .sh)" "$0" "$@"
cd "$(dirname "$0")/.."
OUT=$1
LOGO=$2
GAME=$3
[ -n "$GAME" ] || { echo "usage: sim/logo_gif.sh <out.gif> <logo.mhl> <frame.ppm>" >&2; exit 1; }
docker image inspect mister-hybrid-sim > /dev/null 2>&1 || docker build -t mister-hybrid-sim toolchain/sim
WORK=$(mktemp -d "$PWD/.logo_gif.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
# the testbench's memory: the logo as its overlay frame, the game's frame
python3 - "$LOGO" "$GAME" "$WORK" <<'END' || exit 1
import struct, sys
from PIL import Image
mhl, game, out = sys.argv[1:4]

def words(data):
    data = data + bytes(-len(data) % 8)
    return "".join("%016X\n" % int.from_bytes(data[n:n + 8], "little") for n in range(0, len(data), 8))

def pal_words(cols):
    cols = list(cols) + [(0, 0, 0)] * (256 - len(cols))
    v = [r << 16 | g << 8 | b for r, g, b in cols]
    return "".join("%016X\n" % (v[2 * n + 1] << 32 | v[2 * n]) for n in range(128))

d = open(mhl, "rb").read()
assert d[:4] == b"MHLG", mhl + ": no logo of make_logo.py"
w, h, n = struct.unpack("<HHH", d[4:10])
pal = [tuple(d[10 + 3 * i:13 + 3 * i]) for i in range(n)]
px = d[10 + 3 * n:]
frame = bytearray(640 * 480)
# centred, its bottom edge on the MiSTer logo's (tools/make_logo.py)
x0, y0 = (640 - w) // 2, (480 - 111) // 2 + 111 - h
for y in range(h):
    frame[(y0 + y) * 640 + x0:(y0 + y) * 640 + x0 + w] = px[y * w:(y + 1) * w]
open(out + "/ovl_fb.hex", "w").write(words(bytes(frame)))
open(out + "/ovl_pal.hex", "w").write(pal_words([(0, 0, 0)] + pal))
g = Image.open(game)
if g.size == (640, 480) and g.mode == "P":
    raw = g.getpalette()[:768]
    open(out + "/page_fb.hex", "w").write(words(g.tobytes()))
    open(out + "/page_pal.hex", "w").write(pal_words([tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]))
    open(out + "/page", "w").close()
else:
    g = g.convert("RGB")
    assert g.size == (320, 200), game + ": not 320x200, nor a page of 640x480 with a palette"
    q = g.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    raw = q.getpalette()[:768]
    open(out + "/game_fb.hex", "w").write(words(q.tobytes()))
    open(out + "/game_pal.hex", "w").write(pal_words([tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]))
END
PAGE=0
[ -f "$WORK/page" ] && PAGE=1
STEP=32       # fields per piece: 16 frames
LAST=${LAST:-576}  # the timeline's end: the game's first frame is there (tb_wall.sv, L_*)
s=${FIRST:-0}
while [ "$s" -lt "$LAST" ]; do
  docker run --rm ${DNK_JOB:+--label dnk.job=$DNK_JOB} -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src mister-hybrid-sim sh -c "
    iverilog -g2012 -o /tmp/tbw -Ptb_wall.LOGO=\\\"${WORK#$PWD/}\\\" -Ptb_wall.PAGE=$PAGE -Ptb_wall.START=$s -Ptb_wall.FRAMES=$((STEP / 2)) \
      -Ptb_wall.OUT=\\\"/src/${WORK#$PWD/}/f$(printf %04d "$s")\\\" \
      sim/tb_wall.sv rtl/hybrid_host.sv rtl/hybrid_fm.sv sim/jtopl2_stub.v sim/hybrid_midi_stub.sv template/core/sys/mt32pi.sv 2>/dev/null &&
    vvp -n /tmp/tbw" > "$WORK/log$(printf %04d "$s")" 2>&1 &
  s=$((s + STEP))
done
wait
grep -h "logo timeline" "$WORK"/log* >&2
python3 - "$WORK" "$OUT" "${FIRST:-0}" <<'END'
import glob, sys
from PIL import Image
work, out, first = sys.argv[1], sys.argv[2], int(sys.argv[3])
frames = [Image.open(f).convert("RGB") for f in sorted(glob.glob(work + "/f*.ppm"))]
# the core's first 16 fields are black (from the start), and the last frame
# stays a second
black = Image.new("RGB", (640, 480))
frames = ([black] * 8 if first == 0 else []) + frames + [frames[-1]] * 30
# a GIF counts in hundredths of a second: each frame ends where the real time is
ends = [round(100 * 2 * (n + 1) / 59.64) * 10 for n in range(len(frames))]
durations = [b - a for a, b in zip([0] + ends, ends)]
frames[0].save(out, save_all=True, append_images=frames[1:], duration=durations, loop=0, optimize=False)
print("%s: %d frames, %.1f s" % (out, len(frames), ends[-1] / 1000))
END
