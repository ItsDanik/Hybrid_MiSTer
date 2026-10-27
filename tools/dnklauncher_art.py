#!/usr/bin/env python3
# The pictures and texts of dnkLauncher, the frontend core (docs/dnklauncher.md,
# "Where everything comes from"): made once on the PC from the ports'
# repositories, so that the frontend never scales a picture on the MiSTer. It
# draws each one as it is, at the size the screen shows it.
#
# For every game of a port's package/games.txt:
#   <Engine>/<id>.shot     its screenshot (package/screens/<id>.png, or the
#                          port's docs/screenshot2.png while it has none),
#                          4:3 at the size of SHOT
#   <Engine>/<id>.logo     its logo (package/logos/<id>.png) fitted into the
#                          preview's box (the list's), with its alpha
#   <Engine>/<id>.dlogo    the same fitted into the box of the Details page
#   <Engine>/<id>.plogo    and into the port's panel of the ports page
#   <Engine>/<id>.clogo    and into the Carousel layout's box, over the
#                          selected game (CLOGO_BOX)
# and index.txt, a line a game: <Engine>|<id>|<title>|<year>|<developer>|
# <description> (package/info.txt; empty fields for a game it lacks).
#
# A picture's file: "DNKI", width and height (16 bits each, little endian), its
# format (1 RGB565, 2 RGB565 and then an alpha byte a pixel), three zero bytes,
# then the pixels, RGB565 little endian a row after the other (then the alpha
# bytes). Scaled in linear light with Lanczos, then dithered to RGB565 (4x4
# ordered, which stays the same from one picture to the next, so a logo looks
# the same in the list and in Details); not premultiplied.
#
# The sizes are the mock-ups' (tools/dnklauncher_mockup.py), which are the
# program's.
#
# usage: dnklauncher_art.py <out dir> [--png] [repositories...]
#   without repositories: every ~/MiSTer-* of a port the database at
#   ~/dnk_Distribution_MiSTer has. --png writes each picture also as
#   <name>.png, as the frontend would draw it (to look at). Writes the tree
#   that goes to games/dnk/dnkLauncher/art/ on the SD card.
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dnklauncher_mockup as mock  # noqa: E402

BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 - 15 / 32


# The Carousel layout's logo box (Danik's settings on its tuning page,
# 2026-10-27; CAR_LOGO_W / CAR_LOGO_H of the program's carousel.h)
CLOGO_BOX = (300, 46)


def fit(im, box_w, box_h):
    """The picture cropped to what it draws and scaled into the box, keeping its shape"""
    im = im.crop(im.getbbox())
    s = min(box_w / im.width, box_h / im.height)
    return linear_resize(im, (max(1, round(im.width * s)), max(1, round(im.height * s))))


def linear_resize(im, size):
    """Lanczos in linear light; with alpha, the colour weighted by it so that
    the edges take no colour from the transparent pixels around them"""
    a = np.asarray(im.convert("RGBA"), dtype=np.float64) / 255
    rgb = a[..., :3] ** 2.2 * a[..., 3:]
    planes = [Image.fromarray(p.astype(np.float32), "F").resize(size, Image.LANCZOS)
              for p in [rgb[..., 0], rgb[..., 1], rgb[..., 2], a[..., 3]]]
    rgb = np.stack([np.asarray(p) for p in planes[:3]], -1)
    alpha = np.clip(np.asarray(planes[3]), 0, 1)
    rgb = np.where(alpha[..., None] > 1e-4, rgb / np.maximum(alpha[..., None], 1e-4), 0)
    rgb = np.clip(rgb, 0, 1) ** (1 / 2.2)
    return np.concatenate([rgb, alpha[..., None]], -1)


def rgb565(rgba):
    """The colours dithered to 5, 6 and 5 bits; returns the 16-bit values and what they show"""
    h, w = rgba.shape[:2]
    d = np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w]
    out = []
    for c, bits in zip(range(3), (5, 6, 5)):
        top = (1 << bits) - 1
        out.append(np.clip(np.floor(rgba[..., c] * top + 0.5 + d), 0, top).astype(np.uint16))
    r, g, b = out
    shown = np.stack([r * 255 / 31, g * 255 / 63, b * 255 / 31], -1)
    return (r << 11) | (g << 5) | b, shown


def write(path, rgba, alpha, png):
    h, w = rgba.shape[:2]
    pixels, shown = rgb565(rgba)
    with open(path, "wb") as f:
        f.write(b"DNKI" + struct.pack("<HHB3x", w, h, 2 if alpha else 1))
        f.write(pixels.astype("<u2").tobytes())
        if alpha:
            f.write(np.round(rgba[..., 3] * 255).astype(np.uint8).tobytes())
    if png:
        a = np.round(rgba[..., 3:] * 255) if alpha else np.full((h, w, 1), 255)
        Image.fromarray(np.concatenate([shown, a], -1).round().astype(np.uint8), "RGBA").save(path + ".png")
    return os.path.getsize(path)


def read_info(repo):
    info = {}
    path = os.path.join(repo, "package", "info.txt")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            if "|" in line and not line.startswith("#"):
                gid, year, dev, desc = line.rstrip("\n").split("|", 3)
                info[gid] = (year, dev, desc)
    return info


def main():
    args = sys.argv[1:]
    png = "--png" in args
    args = [a for a in args if a != "--png"]
    if not args:
        sys.exit("usage: dnklauncher_art.py <out dir> [--png] [repositories...]")
    out, repos = args[0], args[1:]
    if not repos:
        import glob
        repos = sorted(glob.glob(os.path.expanduser("~/MiSTer-*")))
    pw = mock.PREV_X[1] - mock.PREV_X[0]
    logo_box = (pw - 24, mock.LOGO_H)
    dlogo_box = mock.details_logo_box()[2:]
    plogo_box = mock.ports_logo_box()
    clogo_box = CLOGO_BOX
    index, total, missing = [], 0, []
    for port in mock.load_ports(repos):
        engine = port["engine"]
        repo = os.path.dirname(os.path.dirname(os.path.dirname(port["games"][0]["logo"])))
        info = read_info(repo)
        os.makedirs(os.path.join(out, engine), exist_ok=True)
        for g in port["games"]:
            gid = g["id"]
            base = os.path.join(out, engine, gid)
            shot = os.path.join(repo, "package", "screens", gid + ".png")
            if not os.path.exists(shot):
                missing.append("%s/%s screenshot" % (engine, gid))
                shot = g["shot"]
            src = Image.open(shot).convert("RGB")
            total += write(base + ".shot", linear_resize(src, mock.SHOT), False, png)
            logo = Image.open(g["logo"]).convert("RGBA")
            total += write(base + ".logo", fit(logo, *logo_box), True, png)
            total += write(base + ".dlogo", fit(logo, *dlogo_box), True, png)
            total += write(base + ".plogo", fit(logo, *plogo_box), True, png)
            total += write(base + ".clogo", fit(logo, *clogo_box), True, png)
            if gid not in info:
                missing.append("%s/%s info" % (engine, gid))
            # tty2oled+'s icon (docs/oled.md), which package.sh also needs
            if not os.path.exists(os.path.join(repo, "package", "oled", gid + "_icon.png")):
                missing.append("%s/%s OLED icon" % (engine, gid))
            year, dev, desc = info.get(gid, ("", "", ""))
            index.append("|".join((engine, gid, g["title"], year, dev, desc)))
    with open(os.path.join(out, "index.txt"), "w", encoding="utf-8") as f:
        f.write("# dnkLauncher's games (hybrid/tools/dnklauncher_art.py): "
                "<Engine>|<id>|<title>|<year>|<developer>|<description>\n")
        f.write("\n".join(index) + "\n")
    print("%d games, %.1f MB" % (len(index), total / 1e6))
    for m in missing:
        print("missing:", m)


if __name__ == "__main__":
    main()
