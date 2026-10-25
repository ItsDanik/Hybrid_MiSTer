#!/usr/bin/env python3
# Make the banner at the top of a port's README.md, all of it in the core's
# purple: the game's logo (440x200, package/logos/<id>.png of the first game of
# package/games.txt) centred at the top, its whole area its own, and under it a
# stripe with the badge as the core's picture has it at its right end:
# "danik Hybrid Framework" in Cozette, right of it the icon assets/danik.png,
# 12 pixels apart. Nothing of the logo reaches into the stripe, so whatever a
# logo is like, the badge has nothing beside it. Drawn in the core's pixels,
# BANNER_W wide, then every pixel made SCALE x SCALE, so that it stays sharp.
#
# With --framework, the banner of the framework's own page (OVERVIEW.md,
# tools/make_overview.py): the core's picture without a game, the MiSTer logo
# (assets/MiSTer.png) centred and the badge as the core has it at the bottom
# right, "Hybrid Core Framework" over "by danik", right-aligned, left of the
# icon.
#
# usage: make_banner.py <logo.png> <out.png>
#        make_banner.py --framework <out.png>
import os
import sys

from PIL import Image

BANNER_W = 900
BACKGROUND = (0x7F, 0x30, 0xA0)  # WALL_BG of hybrid_host.sv, as make_wall.py
INK = (0xEF, 0xEF, 0xEF)         # the text: the white of the MiSTer logo
LOGO_W, LOGO_H = 440, 200
MARGIN = 20                      # of the logo from the top, of the badge from the right edge and the bottom
STRIPE_GAP = 8                   # between the logo's area and the badge
GAP = 12                         # between the text and the icon
TEXT = ["danik Hybrid Framework"]
FRAMEWORK_TEXT = ["Hybrid Core Framework", "by danik"]
MISTER_W, MISTER_H = 429, 111
MISTER_TOP = 36                  # of the MiSTer logo from the top
MISTER_GAP = 12                  # between the MiSTer logo and the badge
LINE_GAP = 2                     # between the two lines of the text, as make_wall.py
CELL_W, CELL_H, ASCENT = 6, 13, 10
SCALE = 2

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def cozette_text(lines):
    """The lines in Cozette's cells of 6x13 (as make_wall.py: one column left
    of where the font has its glyphs), right-aligned and LINE_GAP apart, a mask
    of 0 / 255"""
    glyphs, code = {}, None
    with open(os.path.join(root, "assets", "cozette.bdf")) as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == "ENCODING":
                code = int(w[1])
            elif w[0] == "BBX":
                bbx = list(map(int, w[1:]))
            elif w[0] == "BITMAP":
                rows = []
            elif w[0] == "ENDCHAR":
                if code is not None and 32 <= code <= 126:
                    glyphs[code] = (bbx, rows)
                code = None
            elif code is not None and all(c in "0123456789ABCDEFabcdef" for c in w[0]):
                rows.append((int(w[0], 16), len(w[0]) * 4))
    width = max(len(line) for line in lines) * CELL_W
    mask = Image.new("L", (width, len(lines) * (CELL_H + LINE_GAP) - LINE_GAP), 0)
    for n, line in enumerate(lines):
        x0, y0 = width - len(line) * CELL_W, n * (CELL_H + LINE_GAP)
        for c, char in enumerate(line):
            (bw, bh, bx, by), rows = glyphs[ord(char)]
            top = ASCENT - (by + bh)
            for r, (value, bits) in enumerate(rows):
                for x in range(bw):
                    if value >> (bits - 1 - x) & 1 and 0 <= bx - 1 + x < CELL_W:
                        mask.putpixel((x0 + c * CELL_W + bx - 1 + x, y0 + top + r), 255)
    return mask


if len(sys.argv) != 3:
    sys.exit("usage: make_banner.py <logo.png> <out.png>\n       make_banner.py --framework <out.png>")
framework = sys.argv[1] == "--framework"
if framework:
    logo_path, (logo_w, logo_h), top, gap = os.path.join(root, "assets", "MiSTer.png"), (MISTER_W, MISTER_H), MISTER_TOP, MISTER_GAP
else:
    logo_path, (logo_w, logo_h), top, gap = sys.argv[1], (LOGO_W, LOGO_H), MARGIN, STRIPE_GAP
logo = Image.open(logo_path).convert("RGBA")
if logo.size != (logo_w, logo_h):
    sys.exit("%s is %dx%d, it has to be %dx%d" % ((logo_path,) + logo.size + (logo_w, logo_h)))
icon = Image.open(os.path.join(root, "assets", "danik.png")).convert("RGBA")
text = cozette_text(FRAMEWORK_TEXT if framework else TEXT)

BANNER_H = top + logo_h + gap + icon.height + MARGIN
banner = Image.new("RGBA", (BANNER_W, BANNER_H), BACKGROUND + (255,))
banner.alpha_composite(logo, ((BANNER_W - logo_w) // 2, top))
icon_x, icon_y = BANNER_W - MARGIN - icon.width, top + logo_h + gap
banner.alpha_composite(icon, (icon_x, icon_y))
text_x = icon_x - GAP - text.width
banner.paste(Image.new("RGBA", text.size, INK + (255,)), (text_x, icon_y + (icon.height - text.height) // 2), text)
banner = banner.convert("RGB").resize((BANNER_W * SCALE, BANNER_H * SCALE), Image.NEAREST)
banner.save(sys.argv[2], optimize=True)
print("%s -> %s, %dx%d" % (logo_path, sys.argv[2], banner.width, banner.height))
