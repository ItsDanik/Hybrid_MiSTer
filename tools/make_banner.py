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
# usage: make_banner.py <logo.png> <out.png>
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
TEXT = "danik Hybrid Framework"
CELL_W, CELL_H, ASCENT = 6, 13, 10
SCALE = 2

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def cozette_text(text):
    """The text in Cozette's cells of 6x13 (as make_wall.py: one column left of
    where the font has its glyphs), a mask of 0 / 255"""
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
    mask = Image.new("L", (len(text) * CELL_W, CELL_H), 0)
    for c, char in enumerate(text):
        (bw, bh, bx, by), rows = glyphs[ord(char)]
        top = ASCENT - (by + bh)
        for r, (value, bits) in enumerate(rows):
            for x in range(bw):
                if value >> (bits - 1 - x) & 1 and 0 <= bx - 1 + x < CELL_W:
                    mask.putpixel((c * CELL_W + bx - 1 + x, top + r), 255)
    return mask


if len(sys.argv) != 3:
    sys.exit("usage: make_banner.py <logo.png> <out.png>")
logo = Image.open(sys.argv[1]).convert("RGBA")
if logo.size != (LOGO_W, LOGO_H):
    sys.exit("%s is %dx%d, it has to be %dx%d" % ((sys.argv[1],) + logo.size + (LOGO_W, LOGO_H)))
icon = Image.open(os.path.join(root, "assets", "danik.png")).convert("RGBA")
text = cozette_text(TEXT)

BANNER_H = MARGIN + LOGO_H + STRIPE_GAP + icon.height + MARGIN
banner = Image.new("RGBA", (BANNER_W, BANNER_H), BACKGROUND + (255,))
banner.alpha_composite(logo, ((BANNER_W - LOGO_W) // 2, MARGIN))
icon_x, icon_y = BANNER_W - MARGIN - icon.width, MARGIN + LOGO_H + STRIPE_GAP
banner.alpha_composite(icon, (icon_x, icon_y))
text_x = icon_x - GAP - text.width
banner.paste(Image.new("RGBA", text.size, INK + (255,)), (text_x, icon_y + (icon.height - text.height) // 2), text)
banner = banner.convert("RGB").resize((BANNER_W * SCALE, BANNER_H * SCALE), Image.NEAREST)
banner.save(sys.argv[2], optimize=True)
print("%s -> %s, %dx%d" % (sys.argv[1], sys.argv[2], banner.width, banner.height))
