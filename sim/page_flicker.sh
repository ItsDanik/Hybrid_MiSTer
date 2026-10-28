#!/bin/sh
# The flicker filter on a program's page on the overlay (host version 18,
# the MIDI Player's): a page of 640x480 at 8bpp (index 0 transparent) put up
# as the MIDI Player does (sim/tb_wall.sv with LOGO and PAGE), at 480i, with
# the OSD's Flicker Filter line Off, Light and Strong. Every pixel is checked
# against the filter worked out here: the rows above and below of the page,
# a transparent pixel the purple screen's background; where the page and its
# rows above and below are all transparent, the picture with the line Off.
# usage: sim/page_flicker.sh [<page.png>]   (a page of 640x480 with a
#   palette; without one, a page of lines, boxes and text made here)
# a slot of this PC's simulations, hybrid/tools/jobs.py shows them
[ -n "$DNK_JOB" ] || exec "$(dirname "$0")/../tools/job.sh" sim "$(basename "$0" .sh)" "$0" "$@"
cd "$(dirname "$0")/.."
docker image inspect mister-hybrid-sim > /dev/null 2>&1 || docker build -t mister-hybrid-sim toolchain/sim
WORK=$(mktemp -d "$PWD/.page_flicker.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
python3 - "$WORK" "$1" <<'END' || exit 1
import random, sys
from PIL import Image, ImageDraw
out, src = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ""
if src:
    g = Image.open(src)
    assert g.size == (640, 480) and g.mode == "P", src + ": not a page of 640x480 with a palette"
else:
    # one-pixel lines, boxes, text; clear of the badge (x >= 520, y >= 380)
    g = Image.new("P", (640, 480), 0)
    pal = [0x7F, 0x30, 0xA0, 0xEF, 0xEF, 0xEF, 0xC4, 0xA4, 0xDC, 0x9C, 0x6C, 0xC0, 0x5A, 0x1E, 0x7A, 0xFF, 0xD8, 0x6A]
    g.putpalette(pal + [random.Random(n).randrange(256) for n in range(768 - len(pal))])
    d = ImageDraw.Draw(g)
    rnd = random.Random(1)
    for y in range(0, 470, 7):
        d.line([(10, y), (300, y)], fill=1 + y % 5)
    for n in range(40):
        x, y = rnd.randrange(320, 600), rnd.randrange(0, 360)
        d.rectangle([x, y, x + rnd.randrange(1, 20), y + rnd.randrange(0, 12)], fill=rnd.randrange(1, 256))
    d.text((330, 370), "MIDI Player  Strong Light Off", fill=1)
    d.line([(0, 0), (639, 0)], fill=2)
    d.line([(0, 479), (500, 479)], fill=3)
def words(data):
    return "".join("%016X\n" % int.from_bytes(data[n:n + 8], "little") for n in range(0, len(data), 8))
raw = g.getpalette()[:768]
raw += [0] * (768 - len(raw))
v = [raw[3 * i] << 16 | raw[3 * i + 1] << 8 | raw[3 * i + 2] for i in range(256)]
open(out + "/page_fb.hex", "w").write(words(g.tobytes()))
open(out + "/page_pal.hex", "w").write("".join("%016X\n" % (v[2 * n + 1] << 32 | v[2 * n]) for n in range(128)))
open(out + "/ovl_fb.hex", "w").write(words(bytes(640 * 480)))
open(out + "/ovl_pal.hex", "w").write("0000000000000000\n" * 128)
g.save(out + "/page.png")
END
# the page all in (tb_wall.sv: L_SWAP 489, in by 61 fields later), Strong
# also with Horizontal Size -8 (the same pixels, the size taken); the logo
# (field 345) with -8: the size not taken
run() {  # <name> <start> <line> <size>
  docker run --rm ${DNK_JOB:+--label dnk.job=$DNK_JOB} -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src mister-hybrid-sim sh -c "
    iverilog -g2012 -o /tmp/tbw -Ptb_wall.LOGO=\\\"${WORK#$PWD/}\\\" -Ptb_wall.PAGE=1 -Ptb_wall.START=$2 -Ptb_wall.FRAMES=1 \
      -Ptb_wall.FLICKER=1 -Ptb_wall.FLT=$3 -Ptb_wall.HSIZE=$4 -Ptb_wall.OUT=\\\"/src/${WORK#$PWD/}/$1\\\" \
      sim/tb_wall.sv rtl/hybrid_host.sv rtl/hybrid_fm.sv sim/jtopl2_stub.v sim/hybrid_midi_stub.sv template/core/sys/mt32pi.sv 2>/dev/null &&
    vvp -n /tmp/tbw" > "$WORK/log_$1" 2>&1
}
run f2 570 2 0 & run f0 570 0 0 & run f1 570 1 0 & run s1 570 1 -8 & run logo 345 1 -8 &
wait
cat "$WORK"/log_f2 >&2
grep -q "size -8" "$WORK/log_s1" || { echo "page_flicker: FAIL: the page did not take Horizontal Size"; cat "$WORK/log_s1"; exit 1; }
grep -q "size 0" "$WORK/log_logo" || { echo "page_flicker: FAIL: the logo took Horizontal Size"; cat "$WORK/log_logo"; exit 1; }
cmp -s "$WORK/f1_000.ppm" "$WORK/s1_000.ppm" || { echo "page_flicker: FAIL: the page differs with Horizontal Size -8"; exit 1; }
echo "page_flicker: Horizontal Size -8 taken by the page, the same pixels; not by the logo"
python3 - "$WORK" <<'END'
import sys
from PIL import Image
w = sys.argv[1]
page = Image.open(w + "/page.png")
idx = page.tobytes()
raw = page.getpalette()
bg = (0x7F, 0x30, 0xA0)
def col(i):
    return bg if i == 0 else tuple(raw[3 * i:3 * i + 3])
off = Image.open(w + "/f2_000.ppm").convert("RGB").load()
fails = 0
for flt, name in ((0, "Light"), (1, "Strong")):
    got = Image.open(w + "/f%d_000.ppm" % flt).convert("RGB").load()
    bad = filtered = 0
    for y in range(480):
        yu, yd = max(y - 1, 0), min(y + 1, 479)
        for x in range(640):
            iu, ic, idn = idx[yu * 640 + x], idx[y * 640 + x], idx[yd * 640 + x]
            if iu == ic == idn == 0:
                want = off[x, y]
            else:
                u, c, d = col(iu), col(ic), col(idn)
                if name == "Light":
                    want = tuple((u[k] + 6 * c[k] + d[k] + 4) // 8 for k in range(3))
                else:
                    want = tuple((u[k] + 2 * c[k] + d[k] + 2) // 4 for k in range(3))
                filtered += 1
            if got[x, y] != want:
                if bad < 5:
                    print("%s: (%d,%d) %s, want %s (indices %d %d %d)" % (name, x, y, got[x, y], want, iu, ic, idn))
                bad += 1
    print("page_flicker: %s: %d pixels filtered, %d wrong" % (name, filtered, bad))
    fails += bad
# Off: the page as it is over the background
got = off
bad = 0
for y in range(480):
    for x in range(640):
        i = idx[y * 640 + x]
        if i and got[x, y] != col(i):
            bad += 1
print("page_flicker: Off: %d page pixels wrong" % bad)
fails += bad
sys.exit(1 if fails else 0)
END
