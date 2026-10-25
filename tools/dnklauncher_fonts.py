#!/usr/bin/env python3
# Fonts for dnkLauncher at 640x480, which a 15kHz screen shows interlaced:
# the even lines in one field, the odd ones in the next, 30 times a second
# each. What one field lights and the next does not blinks. This draws the
# same sample in each font (a list row, the cursor's bar, a dimmed row, a line
# of description, on the frontend's colours) and measures that blinking:
#
#   flicker = sum |E - O| / sum |L - ground|
#
# L the picture's lightness (linear), E and O the picture of each field (the
# other field's lines black) blurred vertically ([1 4 6 4 1] / 16, the few
# lines the eye takes together at a TV's distance), the sum over the sample.
# 0 is a picture both fields light alike; a stroke one line high is about 1.
# "filtered" is the same after a flicker filter ([1 2 1] / 4 vertically, what
# the program could do to its own picture on a 15kHz screen only: on HDMI or
# a 31kHz screen 640x480 is progressive and nothing blinks).
#
# usage: dnklauncher_fonts.py <fonts dir> <out dir>
#   the fonts dir: the files FONTS names (Google Fonts, Terminus' and Spleen's
#   BDFs, Departure Mono); Cozette and DejaVu are found where they are. Writes
#   <out>/<key>.png for each font and <out>/fonts.json with the scores.
import json
import os
import sys

from PIL import BdfFontFile, Image, ImageDraw, ImageFont

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PANEL = (0x6A, 0x26, 0x88)
INK = (0xEF, 0xEF, 0xEF)
DIM = (0xB4, 0x94, 0xC6)
BAR = (0xEF, 0xEF, 0xEF)
BAR_INK = (0x5A, 0x1E, 0x76)
W = 640

ROWS = [("row", "Doom II - No Rest for the Living", "today"),
        ("sel", "Quake - Scourge of Armagon", ""),
        ("dim", "Hexen - Deathkings", "no data"),
        ("text", "The demons have reached Earth. Fight your way through", ""),
        ("text", "bases, cities and Hell itself with the super shotgun.", "")]

# key: (label, file, kind, size, options)
#   kind "ttf" (antialiased; size in pixels, a variable font's weight in wght),
#   "pixel" (an outline font of square pixels, drawn sharp at size and then
#   scaled by "scale"), "bdf" (a bitmap font, scaled by "scale")
FONTS = {
    "atkinson": ("Atkinson Hyperlegible Bold", "Atkinson-Bold.ttf", "ttf", 18, {}),
    "atkinson_r": ("Atkinson Hyperlegible", "Atkinson-Regular.ttf", "ttf", 18, {}),
    "nunito": ("Nunito ExtraBold", "Nunito.ttf", "ttf", 18, {"wght": 800}),
    "rubik": ("Rubik Medium", "Rubik.ttf", "ttf", 17, {"wght": 500}),
    "rubik_b": ("Rubik Bold", "Rubik.ttf", "ttf", 17, {"wght": 700}),
    "lexend": ("Lexend SemiBold", "Lexend.ttf", "ttf", 16, {"wght": 600}),
    "exo2": ("Exo 2 Bold", "Exo2.ttf", "ttf", 18, {"wght": 700}),
    "chakra": ("Chakra Petch SemiBold", "ChakraPetch-SemiBold.ttf", "ttf", 18, {}),
    "plex": ("IBM Plex Sans SemiBold", "IBMPlexSans.ttf", "ttf", 17, {"wdth": 100, "wght": 600}),
    "oxanium": ("Oxanium Bold", "Oxanium.ttf", "ttf", 18, {"wght": 700}),
    "sora": ("Sora SemiBold", "Sora.ttf", "ttf", 16, {"wght": 600}),
    "quicksand": ("Quicksand Bold", "Quicksand.ttf", "ttf", 18, {"wght": 700}),
    "orbitron": ("Orbitron Bold", "Orbitron.ttf", "ttf", 14, {"wght": 700}),
    "dejavu_b": ("DejaVu Sans Bold", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", "ttf", 16, {}),
    "pixelify": ("Pixelify Sans", "PixelifySans.ttf", "ttf", 20, {"wght": 500}),
    "vt323": ("VT323", "VT323.ttf", "pixel", 24, {"scale": 1}),
    "pressstart": ("Press Start 2P, x2", "PressStart2P.ttf", "pixel", 8, {"scale": 2}),
    "silkscreen": ("Silkscreen, x2", "Silkscreen.ttf", "pixel", 8, {"scale": 2}),
    "departure": ("Departure Mono, x2", "x/DepartureMono-Regular.otf", "pixel", 11, {"scale": 2}),
    "cozette2": ("Cozette, x2", os.path.join(root, "assets", "cozette.bdf"), "bdf", 0, {"scale": 2}),
    "ter16b": ("Terminus 16 Bold", "x/terminus-font-4.49.1/ter-u16b.bdf", "bdf", 0, {"scale": 1}),
    "ter20b": ("Terminus 20 Bold", "x/terminus-font-4.49.1/ter-u20b.bdf", "bdf", 0, {"scale": 1}),
    "ter24b": ("Terminus 24 Bold", "x/terminus-font-4.49.1/ter-u24b.bdf", "bdf", 0, {"scale": 1}),
    "spleen12": ("Spleen 12x24", "x/spleen-2.1.0/spleen-12x24.bdf", "bdf", 0, {"scale": 1}),
    "mh8x8": ("mh8x8 (now), x2", None, "mh8x8", 0, {"scale": 2}),
}


def load(dirname, key, out):
    label, path, kind, size, opt = FONTS[key]
    if path and not os.path.isabs(path):
        path = os.path.join(dirname, path)
    if kind == "ttf":
        f = ImageFont.truetype(path, size)
        axes = {k: v for k, v in opt.items() if k in ("wght", "wdth")}
        if axes:
            names = [a["name"].decode() if isinstance(a["name"], bytes) else a["name"] for a in f.get_variation_axes()]
            f.set_variation_by_axes([axes.get({"Weight": "wght", "Width": "wdth"}.get(n, n), 400) for n in names])
        return f
    if kind == "pixel":
        return ImageFont.truetype(path, size)
    if kind == "bdf":
        base = os.path.join(out, "_" + key)
        if not os.path.exists(base + ".pil"):
            with open(path, "rb") as fp:
                BdfFontFile.BdfFontFile(fp).save(base)
        return ImageFont.load(base + ".pil")
    return None


def mh8x8_text(text):
    import re
    with open(os.path.join(root, "hps", "mister_font.h")) as f:
        rows = re.findall(r"\{((?:\s*0x[0-9A-Fa-f]{2},?){8})\s*\}", f.read())
    glyphs = [[int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", r)] for r in rows]
    im = Image.new("L", (len(text) * 8, 8), 0)
    for c, ch in enumerate(text):
        for y, bits in enumerate(glyphs[ord(ch) - 32]):
            for x in range(8):
                if bits >> x & 1:
                    im.putpixel((c * 8 + x, y), 255)
    return im


def text_mask(key, font, text):
    """The text as a mask of 0..255, its height the font's line"""
    label, path, kind, size, opt = FONTS[key]
    scale = opt.get("scale", 1)
    if kind == "mh8x8":
        m = mh8x8_text(text)
    else:
        asc, desc = font.getmetrics() if hasattr(font, "getmetrics") else (font.getbbox("Hg")[3], 0)
        h = asc + desc
        wid = int(font.getlength(text)) + 2
        m = Image.new("L", (wid, h), 0)
        d = ImageDraw.Draw(m)
        if kind != "ttf":
            d.fontmode = "1"
        d.text((0, 0), text, font=font, fill=255)
    if scale != 1:
        m = m.resize((m.width * scale, m.height * scale), Image.NEAREST)
    return m


def sample(key, font):
    masks = [(kind, text_mask(key, font, t), text_mask(key, font, tag) if tag else None) for kind, t, tag in ROWS]
    line = max(m.height for _, m, _ in masks)
    pitch = line + 6
    im = Image.new("RGB", (W, 16 + pitch * len(ROWS) + 10), PANEL)
    y = 16
    for kind, m, tag in masks:
        if kind == "sel":
            ImageDraw.Draw(im).rectangle((12, y - 3, W - 13, y + pitch - 4), fill=BAR)
        colour = {"row": INK, "sel": BAR_INK, "dim": DIM, "text": INK}[kind]
        im.paste(Image.new("RGB", m.size, colour), (24, y), m)
        if tag:
            im.paste(Image.new("RGB", tag.size, DIM if kind != "sel" else BAR_INK), (W - 24 - tag.width, y), tag)
        y += pitch
    return im, line


def lightness(im):
    px = list(im.convert("RGB").getdata())
    return [((0.2126 * (r / 255) ** 2.2) + (0.7152 * (g / 255) ** 2.2) + (0.0722 * (b / 255) ** 2.2)) for r, g, b in px]


def vblur(values, w, h, kernel):
    k = len(kernel) // 2
    s = sum(kernel)
    out = [0.0] * (w * h)
    for y in range(h):
        for x in range(w):
            acc = 0.0
            for i, kv in enumerate(kernel):
                yy = min(h - 1, max(0, y + i - k))
                acc += kv * values[yy * w + x]
            out[y * w + x] = acc / s
    return out


def flicker(im):
    w, h = im.size
    L = lightness(im)
    ground = sorted(L)[len(L) // 2]          # the panel: most of the sample
    E = [v if (i // w) % 2 == 0 else 0.0 for i, v in enumerate(L)]
    O = [v if (i // w) % 2 == 1 else 0.0 for i, v in enumerate(L)]
    # each field is shown on its own: twice its lines' light, half the time
    Eb = vblur(E, w, h, [1, 4, 6, 4, 1])
    Ob = vblur(O, w, h, [1, 4, 6, 4, 1])
    ink = sum(abs(v - ground) for v in L)
    return 2 * sum(abs(a - b) for a, b in zip(Eb, Ob)) / ink


def flicker_filter(im):
    w, h = im.size
    bands = [list(b.getdata()) for b in im.split()]
    out = [vblur(b, w, h, [1, 2, 1]) for b in bands]
    return Image.merge("RGB", [Image.frombytes("L", (w, h), bytes(int(round(v)) for v in b)) for b in out])


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: dnklauncher_fonts.py <fonts dir> <out dir>")
    fonts_dir, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    scores = {}
    for key in FONTS:
        font = load(fonts_dir, key, out)
        im, line = sample(key, font)
        filt = flicker_filter(im)
        im.save(os.path.join(out, key + ".png"))
        filt.save(os.path.join(out, key + "_filtered.png"))
        cols = W // max(1, text_mask(key, font, "m").width)
        scores[key] = {"label": FONTS[key][0], "flicker": round(flicker(im), 3),
                       "filtered": round(flicker(filt), 3), "line": line,
                       "chars": round(len(ROWS[0][1]) * 1.0 * (W - 48) / text_mask(key, font, ROWS[0][1]).width, 1)}
        print("%-28s flicker %.3f  filtered %.3f  line %2d  chars/row %.0f" % (
            FONTS[key][0], scores[key]["flicker"], scores[key]["filtered"], line, scores[key]["chars"]))
    with open(os.path.join(out, "fonts.json"), "w") as f:
        json.dump(scores, f, indent=1)


if __name__ == "__main__":
    main()
