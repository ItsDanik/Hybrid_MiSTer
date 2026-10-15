#!/usr/bin/env python3
# Make what the core shows while no game is attached (hybrid_host.sv), a
# screen of 640x480 on a background of one colour:
#   the logo     assets/MiSTer.png, 429x111, in the middle of the screen
#   the badge    "Hybrid Core Framework v<VERSION>" over "by danik", both
#                right-aligned, and the icon assets/danik.png (53x54) right of
#                them, 12 pixels apart, in the bottom right corner, 34 pixels
#                from the right edge and from the bottom: a 15kHz screen hides
#                some of the picture's edges
# into
#   rtl/hybrid_wall.hex      the pixels of the logo, then those of the badge
#                            (BADGE_W x BADGE_H, the text and the icon at its
#                            right end), 4 bits each
#   rtl/hybrid_wall_pal.hex  their colours as RRGGBB: 16 for the logo, then
#                            16 for the badge (the last 4: the text's shades)
#   rtl/hybrid_wall_dots.hex the 24 dots under "MiSTer", which light up in turn
#                            during the intro: for each column of the logo
#                            the dot it is in, 0..23, or 1F
#   rtl/hybrid_wall_ease.hex the ease-out curve of the movements of the
#                            intro (not the shine's): 257 values, 0..256 of
#                            the way for 0..256 of the time
#   rtl/hybrid_wall_font.hex the font of the badge's text and of the messages:
#                            characters 32..126 in cells of 6x13, 13 rows of
#                            each, 6 bits per row (bit 0 the leftmost pixel)
#   rtl/hybrid_wall_alone.hex the core's own message when no game comes
#                            (TIMEOUT), as the 40 words of a message's lines
#
# Messages (MH_UI_Message() and the like) are written under the logo: MSG_ROWS
# lines of MSG_COLS characters, the first 16 pixels below the logo, left-
# aligned with it, MSG_PITCH pixels from line to line, above the badge.
# The background is not in them: the core draws it. Where the pictures are
# transparent it shows, their edges are mixed with it. The intro's timing is
# hybrid_host.sv's (WALL_T_*).
#
# usage: make_wall.py [--font <name> --size <pixels> [--sharp 1]] [--out <dir>] [--preview <file.png>]
#   --font     to try another font for the badge's text (Cozette without it,
#              in its own cells of 6x13): "aileron" (Pillow's own),
#              "mh8x8" (the 8x8 font of the hybrid core screens,
#              hps/mister_font.h; no size) or a font file
#   --sharp 1  every pixel of the text on or off, no shades: for a pixel
#              font at the size it was drawn for
#   --out      write the files there instead of rtl/
#   --preview  also write the screen as the core shows it, from the files made
#   --message  "line|line|..." on that screen, as the core shows a message;
#              "timeout" for the core's own, when no game comes
# VERSION is the one of hybrid_host.sv: run this again when that changes. The
# sizes and positions below are hybrid_host.sv's too (WALL_* there).
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

SCREEN_W, SCREEN_H = 640, 480
BACKGROUND = (0x7F, 0x30, 0xA0)  # WALL_BG of hybrid_host.sv
INK = (0xEF, 0xEF, 0xEF)         # the text: the white of the logo
LOGO_W, LOGO_H = 429, 111
BADGE_W, BADGE_H = 320, 54
MARGIN = 34                      # of the badge, from the right edge and the bottom
GAP = 12                         # between the text and the icon
LINE_GAP = 2                     # between the two lines of the text
# Cozette (assets/cozette.bdf, MIT licence: assets/cozette-LICENSE.txt), a
# pixel font of 6x13: every pixel of the text is on or off, sharp on a CRT
FONT, FONT_SIZE, SHARP = "cozette", 13, True
CELL_W, CELL_H, ASCENT = 6, 13, 10
# where messages go, as hybrid_host.sv has it (WALL_MSG_*)
MSG_X = (SCREEN_W - LOGO_W) // 2                  # the logo's left edge
MSG_Y = (SCREEN_H - LOGO_H) // 2 + LOGO_H + 16    # 16 below the logo
MSG_PITCH = 16
MSG_COLS = LOGO_W // CELL_W                       # as wide as the logo
MSG_ROWS = 4
# the core's own message, when no game comes
TIMEOUT = ["Game not running.", "Please run danik_hybrid_ports.sh from the Scripts folder."]
COLOURS = 16
# the dots: rows of the grey bar above and below them, and the rows they are
# in (WALL_DOT_Y0 / Y1 of hybrid_host.sv, which lights the pixels of them that
# are darker than 160 of 255)
DOT_BAR_ROWS = (89, 99)
DOT_ROWS = (91, 97)
DOTS = 24
DARK = 160
# the ease-out curve, a cubic Bezier as in CSS: cubic-bezier(x1, y1, x2, y2)
EASE = (0.33, 1.0, 0.68, 1.0)    # "easeOutCubic"

args = sys.argv[1:]
options = {}
while args:
    if len(args) < 2 or args[0] not in ("--font", "--size", "--sharp", "--out", "--preview", "--message"):
        sys.exit("usage: make_wall.py [--font <name> --size <pixels> [--sharp 1]] [--out <dir>] [--preview <file.png>]")
    options[args[0]] = args[1]
    args = args[2:]
FONT = options.get("--font", FONT)
FONT_SIZE = int(options.get("--size", FONT_SIZE))
if "--font" in options:
    SHARP = options.get("--sharp", "0") == "1"

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def load(name, size):
    path = os.path.join(root, "assets", name)
    image = Image.open(path).convert("RGBA")
    if image.size != size:
        sys.exit("%s is %dx%d, it has to be %dx%d" % ((path,) + image.size + size))
    return image


def cozette():
    """Cozette's glyphs of 32..126: 13 rows of 6 bits each, bit 0 the leftmost,
    in cells of 6x13 (one column left of where the font has them: every glyph
    has an empty column on its left, and two have one on their right only)"""
    glyphs = {}
    code = None
    with open(os.path.join(root, "assets", "cozette.bdf")) as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == "ENCODING":
                code = int(w[1])
            elif w[0] == "BBX":
                bw, bh, bx, by = map(int, w[1:])
            elif w[0] == "BITMAP":
                bits, rows = [0] * CELL_H, []
            elif w[0] == "ENDCHAR":
                if 32 <= code <= 126:
                    top = ASCENT - (by + bh)
                    for r, value in enumerate(rows):
                        for x in range(bw):
                            if value >> (len(w_hex) * 4 - 1 - x) & 1 and 0 <= bx - 1 + x < CELL_W:
                                bits[top + r] |= 1 << (bx - 1 + x)
                    glyphs[code] = bits
                code = None
            elif code is not None and re.fullmatch(r"[0-9A-Fa-f]+", w[0]):
                w_hex = w[0]
                rows.append(int(w[0], 16))
    return glyphs


def cell_text(lines, width=None):
    """Lines in Cozette's cells, right-aligned, LINE_GAP apart (or left-aligned
    and `pitch` apart for a message): a picture of 0 / 255"""
    glyphs = cozette()
    width = width or max(len(line) for line in lines) * CELL_W
    mask = Image.new("L", (width, len(lines) * (CELL_H + LINE_GAP) - LINE_GAP), 0)
    for n, line in enumerate(lines):
        x0 = width - len(line) * CELL_W
        for c, char in enumerate(line):
            for y, bits in enumerate(glyphs[ord(char)]):
                for x in range(CELL_W):
                    if bits >> x & 1:
                        mask.putpixel((x0 + c * CELL_W + x, n * (CELL_H + LINE_GAP) + y), 255)
    return mask


def text_mask(lines, size):
    """The lines, right-aligned and one under the other, as shades of 0..255
    in a picture of the size of their block"""
    if FONT == "cozette":
        return cell_text(lines)
    if FONT == "mh8x8":
        with open(os.path.join(root, "hps", "mister_font.h")) as f:
            rows = re.findall(r"\{((?:\s*0x[0-9A-Fa-f]{2},?){8})\s*\}", f.read())
        glyphs = [[int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", r)] for r in rows]
        height = 8
        width = max(len(line) for line in lines) * 8
        mask = Image.new("L", (width, len(lines) * (height + LINE_GAP) - LINE_GAP), 0)
        for n, line in enumerate(lines):
            x0 = width - len(line) * 8
            for c, char in enumerate(line):
                for y, bits in enumerate(glyphs[ord(char) - 32]):
                    for x in range(8):
                        if bits >> x & 1:
                            mask.putpixel((x0 + c * 8 + x, n * (height + LINE_GAP) + y), 255)
        return mask
    font = ImageFont.load_default(size=size) if FONT == "aileron" else ImageFont.truetype(FONT, size)
    # every line as high as the font's capitals and descenders, whatever it has
    top = font.getbbox("H")[1]
    height = font.getbbox("Hy")[3] - top
    width = max(font.getbbox(line)[2] for line in lines)
    mask = Image.new("L", (width, len(lines) * (height + LINE_GAP) - LINE_GAP), 0)
    draw = ImageDraw.Draw(mask)
    if SHARP:
        draw.fontmode = "1"
    for n, line in enumerate(lines):
        draw.text((width - font.getbbox(line)[2], n * (height + LINE_GAP) - top), line, font=font, fill=255)
    return mask


def quantize(image, colours):
    """An RGBA picture on the background: pixel indexes 0..colours-1 (0 is the
    background, where the picture is transparent) and their colours"""
    flat = Image.new("RGBA", image.size, BACKGROUND + (255,))
    flat.alpha_composite(image)
    q = flat.convert("RGB").quantize(colours - 1, method=Image.MAXCOVERAGE, kmeans=4, dither=Image.NONE)
    palette = [BACKGROUND] + [tuple(q.getpalette()[n * 3 : n * 3 + 3]) for n in range(colours - 1)]
    alpha = image.getchannel("A").tobytes()
    pixels = [0 if a == 0 else p + 1 for p, a in zip(q.tobytes(), alpha)]
    return pixels, palette


with open(os.path.join(root, "rtl", "hybrid_host.sv")) as f:
    version = re.search(r"VERSION\s*=\s*32'd(\d+)", f.read()).group(1)
lines = ["Hybrid Core Framework v" + version, "by danik"]

# the logo
logo_pixels, logo_pal = quantize(load("MiSTer.png", (LOGO_W, LOGO_H)), COLOURS)

# the badge: the icon in its own colours and the text in shades of INK, four
# of them from the background up
icon = load("danik.png", (53, 54))
SHADES = 4
icon_pixels, icon_pal = quantize(icon, COLOURS - SHADES)
block = text_mask(lines, FONT_SIZE)
text_x = BADGE_W - icon.width - GAP - block.width
if text_x < 0 or block.height > BADGE_H:
    sys.exit("the text is larger than the badge (%dx%d pixels)" % (BADGE_W - icon.width - GAP, BADGE_H))
mask = Image.new("L", (BADGE_W, BADGE_H), 0)
mask.paste(block, (text_x, (BADGE_H - block.height) // 2))
badge_pal = icon_pal + [tuple(round(b + (i - b) * n / SHADES) for b, i in zip(BACKGROUND, INK)) for n in range(1, SHADES + 1)]
badge_pixels = []
for y in range(BADGE_H):
    for x in range(BADGE_W):
        ix = x - (BADGE_W - icon.width)
        if ix >= 0:
            badge_pixels.append(icon_pixels[y * icon.width + ix])
        else:
            level = (mask.getpixel((x, y)) * SHADES + 127) // 255
            badge_pixels.append(0 if level == 0 else COLOURS - SHADES - 1 + level)

out = options.get("--out", os.path.join(root, "rtl"))
with open(os.path.join(out, "hybrid_wall_pal.hex"), "w") as f:
    for colour in logo_pal + badge_pal:
        f.write("%02X%02X%02X\n" % colour)
with open(os.path.join(out, "hybrid_wall.hex"), "w") as f:
    f.write("".join("%X\n" % p for p in logo_pixels + badge_pixels))
print("assets/MiSTer.png, assets/danik.png, \"%s\" (%s) -> %s/hybrid_wall.hex, hybrid_wall_pal.hex"
      % (" / ".join(lines), FONT if FONT == "mh8x8" else "%s %d" % (FONT, FONT_SIZE), out))

# the dots: columns where the bar is above and below and something quite dark
# is between, in runs; each run widened by a column on both sides for the edge
logo = load("MiSTer.png", (LOGO_W, LOGO_H))
lum = lambda p: (p[0] * 299 + p[1] * 587 + p[2] * 114) // 1000 if p[3] > 128 else 0
bar = lum(logo.getpixel((LOGO_W // 2, DOT_BAR_ROWS[0])))
def in_dot(x):
    return all(abs(lum(logo.getpixel((x, y))) - bar) < 8 for y in DOT_BAR_ROWS) and \
        any(lum(logo.getpixel((x, y))) < DARK // 2 for y in range(DOT_ROWS[0], DOT_ROWS[1] + 1))
runs = []
for x in range(LOGO_W):
    if in_dot(x):
        if runs and runs[-1][1] == x - 1:
            runs[-1][1] = x
        else:
            runs.append([x, x])
if len(runs) != DOTS:
    sys.exit("found %d dots in the logo, not %d" % (len(runs), DOTS))
columns = [0x1F] * LOGO_W
for n, (a, b) in enumerate(runs):
    for x in range(max(a - 1, 0), min(b + 2, LOGO_W)):
        columns[x] = n
with open(os.path.join(out, "hybrid_wall_dots.hex"), "w") as f:
    f.write("".join("%02X\n" % c for c in columns))

# the curve: for each 1/256 of the time, the point of the curve there
def bezier(t, a, b):
    return 3 * (1 - t) ** 2 * t * a + 3 * (1 - t) * t * t * b + t ** 3
with open(os.path.join(out, "hybrid_wall_ease.hex"), "w") as f:
    for n in range(257):
        lo, hi = 0.0, 1.0
        for _ in range(40):
            mid = (lo + hi) / 2
            lo, hi = (mid, hi) if bezier(mid, EASE[0], EASE[2]) < n / 256 else (lo, mid)
        f.write("%03X\n" % round(256 * bezier((lo + hi) / 2, EASE[1], EASE[3])))
# the font, for the messages
glyphs = cozette()
with open(os.path.join(out, "hybrid_wall_font.hex"), "w") as f:
    f.write("".join("%02X\n" % bits for code in range(32, 127) for bits in glyphs[code]))
if any(len(line) > MSG_COLS for line in TIMEOUT) or len(TIMEOUT) > MSG_ROWS:
    sys.exit("the timeout message does not fit")
# the core's own message, as a message block's 4 lines of 80 bytes
text = b"".join(line.encode().ljust(80, b"\0") for line in TIMEOUT + [""] * (MSG_ROWS - len(TIMEOUT)))
with open(os.path.join(out, "hybrid_wall_alone.hex"), "w") as f:
    f.write("".join("%016X\n" % int.from_bytes(text[n : n + 8], "little") for n in range(0, len(text), 8)))

print("dots at %s; ease-out cubic-bezier%s -> hybrid_wall_dots.hex, hybrid_wall_ease.hex, hybrid_wall_font.hex, hybrid_wall_alone.hex" % (
    " ".join(str(a) for a, b in runs), EASE))

if "--preview" in options:
    screen = Image.new("RGB", (SCREEN_W, SCREEN_H), BACKGROUND)
    for pixels, palette, w, h, x0, y0 in (
        (logo_pixels, logo_pal, LOGO_W, LOGO_H, (SCREEN_W - LOGO_W) // 2, (SCREEN_H - LOGO_H) // 2),
        (badge_pixels, badge_pal, BADGE_W, BADGE_H, SCREEN_W - MARGIN - BADGE_W, SCREEN_H - MARGIN - BADGE_H),
    ):
        part = Image.new("RGB", (w, h))
        part.putdata([palette[p] for p in pixels])
        screen.paste(part, (x0, y0))
    if "--message" in options:
        lines = TIMEOUT if options["--message"] == "timeout" else options["--message"].split("|")
        for n, line in enumerate(lines[:MSG_ROWS]):
            for c, char in enumerate(line[:MSG_COLS]):
                for y, bits in enumerate(glyphs[ord(char) if 32 <= ord(char) <= 126 else ord("?")]):
                    for x in range(CELL_W):
                        if bits >> x & 1:
                            screen.putpixel((MSG_X + c * CELL_W + x, MSG_Y + n * MSG_PITCH + y), INK)
    screen.save(options["--preview"])
    print("preview -> %s" % options["--preview"])
