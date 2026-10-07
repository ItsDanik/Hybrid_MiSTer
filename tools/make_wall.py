#!/usr/bin/env python3
# Make what the core shows while no game is attached (hybrid_host.sv):
#   rtl/hybrid_wall.hex       the logo, 244x64 pixels of 4 bits
#   rtl/hybrid_wall_pal.hex   their 16 colours as RRGGBB
#   rtl/hybrid_wall_text.hex  "danik HCF v<VERSION>" for the bottom right
#                             corner, 7 rows of 96 pixels (and an empty one): 1 bit each, bit 0 is
#                             the leftmost and the text ends at the last one
# The background is not in them: the core draws it.
#
# usage: make_wall.py [logo.png]     (default: wallsmall.png of this repository)
# The logo is 244x64; where it is transparent the background shows.
# VERSION is the one of hybrid_host.sv: run this again when that changes.
import os
import re
import sys

from PIL import Image

WIDTH, HEIGHT, COLOURS = 244, 64, 16
BACKGROUND = (0x7F, 0x30, 0xA0)  # WALL_BG of hybrid_host.sv
TEXT_WIDTH = 96

# 5x7, the characters of the text only
FONT = {
    " ": "..... ..... ..... ..... ..... ..... .....",
    "0": ".###. #...# #..## #.#.# ##..# #...# .###.",
    "1": "..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.",
    "2": ".###. #...# ....# ...#. ..#.. .#... #####",
    "3": "##### ...#. ..#.. ...#. ....# #...# .###.",
    "4": "...#. ..##. .#.#. #..#. ##### ...#. ...#.",
    "5": "##### #.... ####. ....# ....# #...# .###.",
    "6": "..##. .#... #.... ####. #...# #...# .###.",
    "7": "##### ....# ...#. ..#.. .#... .#... .#...",
    "8": ".###. #...# #...# .###. #...# #...# .###.",
    "9": ".###. #...# #...# .#### ....# ...#. .##..",
    "C": ".###. #...# #.... #.... #.... #...# .###.",
    "F": "##### #.... #.... ####. #.... #.... #....",
    "H": "#...# #...# #...# ##### #...# #...# #...#",
    "a": "..... ..... .###. ....# .#### #...# .####",
    "d": "....# ....# .##.# #..## #...# #...# .####",
    "i": "..#.. ..... .##.. ..#.. ..#.. ..#.. .###.",
    "k": "#.... #.... #..#. #.#.. ##... #.#.. #..#.",
    "n": "..... ..... #.##. ##..# #...# #...# #...#",
    "v": "..... ..... #...# #...# #...# .#.#. ..#..",
}

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
source = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "wallsmall.png")

logo = Image.open(source).convert("RGBA")
if logo.size != (WIDTH, HEIGHT):
    sys.exit("%s is %dx%d, the logo is %dx%d" % ((source,) + logo.size + (WIDTH, HEIGHT)))
image = Image.new("RGBA", logo.size, BACKGROUND + (255,))
image.alpha_composite(logo)
image = image.convert("RGB").quantize(COLOURS, method=Image.MAXCOVERAGE, kmeans=4, dither=Image.NONE)

palette = image.getpalette()[: COLOURS * 3]
palette += [0] * (COLOURS * 3 - len(palette))
with open(os.path.join(root, "rtl", "hybrid_wall_pal.hex"), "w") as f:
    for n in range(COLOURS):
        f.write("%02X%02X%02X\n" % tuple(palette[n * 3 : n * 3 + 3]))

pixels = image.tobytes()
with open(os.path.join(root, "rtl", "hybrid_wall.hex"), "w") as f:
    for row in range(HEIGHT):
        f.write("".join("%X\n" % p for p in pixels[row * WIDTH : (row + 1) * WIDTH]))

with open(os.path.join(root, "rtl", "hybrid_host.sv")) as f:
    version = re.search(r"VERSION\s*=\s*32'd(\d+)", f.read()).group(1)
text = "danik HCF v" + version
left = TEXT_WIDTH - len(text) * 6 + 1
if left < 0:
    sys.exit("the text is wider than %d pixels" % TEXT_WIDTH)
with open(os.path.join(root, "rtl", "hybrid_wall_text.hex"), "w") as f:
    # the memory has 8 rows: one more, empty
    for y in range(8):
        bits = 0
        for n, char in enumerate(text):
            for x, dot in enumerate((FONT[char] + " .....").split()[y]):
                if dot == "#":
                    bits |= 1 << (left + n * 6 + x)
        f.write("%024X\n" % bits)

print("%s -> rtl/hybrid_wall.hex, rtl/hybrid_wall_pal.hex, rtl/hybrid_wall_text.hex" % source)
