#!/usr/bin/env python3
# A game's logo for the start of every port (host version 17, docs/game_logo.md):
# a PNG of 440x200 into the file the library shows over the core's picture
# (the overlay): centred, its bottom edge where the MiSTer logo's is.
#
# The overlay knows only a transparent pixel (palette index 0) and solid ones,
# so a half-transparent pixel is mixed with the core's background (7F30A0)
# here, and a fully transparent one is index 0. The rest is reduced to at most
# 255 colours (Floyd-Steinberg), with a warning when the logo has more.
#
# usage: make_logo.py <logo.png> <out.mhl> [--preview <file.png> [--title <title>]]
#   --preview  also the screen as the core shows it under the logo: the badge
#              and the lines "Checking Game Data...", "Game Data OK",
#              "Loading <title>..." (make_wall.py's picture)
#
# The .mhl file, all numbers little-endian:
#   "MHLG", u16 width, u16 height, u16 colours (n, 1..255),
#   n x RGB (the colours of indexes 1..n), width x height bytes (0 transparent)
import os
import struct
import subprocess
import sys
import tempfile

from PIL import Image

LOGO_W, LOGO_H = 440, 200
BACKGROUND = (0x7F, 0x30, 0xA0)  # WALL_BG of hybrid_host.sv
# the MiSTer logo, 429x111 in the middle of the screen (WALL_LOGO_* of hybrid_host.sv)
MISTER_W, MISTER_H = 429, 111
MISTER_X, MISTER_Y = (640 - MISTER_W) // 2, (480 - MISTER_H) // 2
# the game's: centred, its bottom edge on the MiSTer logo's
LOGO_X, LOGO_Y = (640 - LOGO_W) // 2, MISTER_Y + MISTER_H - LOGO_H


def data(image):
    """The pixels of `image`, a list (getdata() is deprecated in newer Pillow)."""
    return list(image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata())


def convert(path):
    """The logo as (palette [(r, g, b)] for indexes 1.., pixels [index])."""
    image = Image.open(path).convert("RGBA")
    if image.size != (LOGO_W, LOGO_H):
        sys.exit("%s: %dx%d, a logo is %dx%d" % (path, image.width, image.height, LOGO_W, LOGO_H))
    rgba = data(image)
    clear = [a == 0 for _, _, _, a in rgba]
    if all(clear):
        sys.exit("%s: nothing in it" % path)
    flat = Image.new("RGB", image.size, BACKGROUND)
    flat.paste(image, mask=image)
    colours = {p for p, c in zip(data(flat), clear) if not c}
    if len(colours) <= 255:
        palette = sorted(colours)
        index = {p: n + 1 for n, p in enumerate(palette)}
        pixels = [0 if c else index[p] for p, c in zip(data(flat), clear)]
        return palette, pixels
    print("%s: %d colours, reduced to 255" % (path, len(colours)), file=sys.stderr)
    # the palette from the logo's own pixels, then the whole picture mapped to it
    solid = [p for p, c in zip(data(flat), clear) if not c]
    strip = Image.new("RGB", (len(solid), 1))
    strip.putdata(solid)
    pal_image = strip.quantize(255, method=Image.Quantize.MEDIANCUT)
    mapped = flat.quantize(palette=pal_image, dither=Image.Dither.FLOYDSTEINBERG)
    raw = pal_image.getpalette()[:255 * 3]
    palette = [tuple(raw[n:n + 3]) for n in range(0, len(raw), 3)]
    pixels = [0 if c else i + 1 for i, c in zip(data(mapped), clear)]
    return palette, pixels


def write(out, palette, pixels):
    with open(out, "wb") as f:
        f.write(b"MHLG" + struct.pack("<HHH", LOGO_W, LOGO_H, len(palette)))
        for rgb in palette:
            f.write(bytes(rgb))
        f.write(bytes(pixels))


def preview(out, palette, pixels, title):
    here = os.path.dirname(os.path.abspath(__file__))
    with tempfile.TemporaryDirectory() as work:
        shot = os.path.join(work, "wall.png")
        lines = "Checking Game Data...|Game Data OK|Loading %s..." % title
        # make_wall.py's picture with the lines, its files in the scratch folder
        subprocess.run([sys.executable, os.path.join(here, "make_wall.py"), "--out", work, "--preview", shot,
                        "--message", lines], check=True, stdout=subprocess.DEVNULL)
        screen = Image.open(shot).convert("RGB")
    # the MiSTer logo is gone by then: the background where it was
    screen.paste(BACKGROUND, (MISTER_X, MISTER_Y, MISTER_X + MISTER_W, MISTER_Y + MISTER_H))
    for n, i in enumerate(pixels):
        if i:
            screen.putpixel((LOGO_X + n % LOGO_W, LOGO_Y + n // LOGO_W), palette[i - 1])
    screen.save(out)


def main(args):
    options = {}
    rest = []
    while args:
        if args[0] in ("--preview", "--title") and len(args) > 1:
            options[args[0]] = args[1]
            args = args[2:]
        else:
            rest.append(args.pop(0))
    if len(rest) != 2:
        sys.exit("usage: make_logo.py <logo.png> <out.mhl> [--preview <file.png> [--title <title>]]")
    palette, pixels = convert(rest[0])
    write(rest[1], palette, pixels)
    print("%s -> %s: %d colours, %d of %d pixels transparent" % (rest[0], rest[1], len(palette),
          pixels.count(0), len(pixels)))
    if "--preview" in options:
        preview(options["--preview"], palette, pixels, options.get("--title", "the game"))
        print("preview -> %s" % options["--preview"])


main(sys.argv[1:])
