#!/usr/bin/env python3
# A game's pictures and text for tty2oled+'s OLED display (docs/oled.md): the
# SSD1322 panel of 256x64, 16 levels of grey, that tty2oled+ drives over USB.
# Each game of a port has a logo (256x64, the whole panel) and an icon (86x64,
# beside its title, year and developer), shipped as .gsc files in the game's
# folder, games/dnk/<Engine>/oled/<id>_logo.gsc and <id>_icon.gsc, with
# info.txt next to them. tty2oled+ looks for them there; nothing of tty2oled+
# goes onto a MiSTer that does not have it.
#
# usage:
#   make_oled.py logo <src.png> <out.png|.gsc> [--crop x,y,w,h] [--margin n|h,v]
#       the logo: a picture of any size (the game's 440x200 logo of
#       package/logos/) cropped to what is not transparent (or not black), on
#       black, in grey, its contrast stretched, fitted into 256x64 a pixel off
#       every edge and centred
#   make_oled.py icon <src.png> <out.png|.gsc> [--crop x,y,w,h] [--no-stretch] [--margin n|h,v]
#       the icon the same way, fitted into 86x64 two pixels off every edge (a
#       picture of that size and already in the panel's 16 levels is taken as
#       it is, unless --margin is given: then it is fitted again)
#   --margin: the pixels kept clear off every edge (n), or off the left and
#       right (h) and the top and bottom (v) edges (Danik, 2026-10-26: a pixel
#       round a logo, two round an icon)
#   make_oled.py gsc2png <in.gsc> <out.png>
#       a .gsc as a PNG in the panel's levels, nothing lost (the levels are
#       0, 17, ... 255): how the pictures tty2oled+ shipped came into the ports
#   make_oled.py package <package dir> <out dir>
#       what package.sh ships, for every game of <package dir>/games.txt:
#       <out dir>/oled/<id>_logo.gsc from package/oled/<id>_logo.png if there
#       is one, else made from package/logos/<id>.png; <out dir>/oled/
#       <id>_icon.gsc from package/oled/<id>_icon.png, which every game has;
#       <out dir>/info.txt, the lines of package/info.txt in ASCII (tty2oled+
#       draws nothing else). Fails on anything missing.
#       It writes <out dir>/oled/framework_logo.gsc as well, from
#       assets/oled_framework.png: what the display shows while the core's
#       list of games is up (/tmp/dnk_game "<Engine>|", MH_GameList())
#   make_oled.py framework <out.png>
#       draws assets/oled_framework.png: the badge as the framework's banner
#       has it (tools/make_banner.py --framework), "Hybrid Core Framework" over
#       "by danik" left of the icon assets/danik.png, centred on the panel
#   make_oled.py preview <package dir> <out.png> [--scale n]
#       every game's logo and icon as the panel shows them (the logo, then
#       the icon beside the title, year and developer), for a look and for
#       Danik's review
#
# A .gsc is three lines of header and then one hex digit a pixel, row by row:
#   #define icon_width 256
#   #define icon_height 64
#   static unsigned char icon_bits[] = {
#   00000000...
import os
import re
import sys
import unicodedata

from PIL import Image, ImageDraw, ImageOps

LOGO_W, LOGO_H = 256, 64
ICON_W, ICON_H = 86, 64
# a logo's darkest parts are lifted a little: the panel's lowest levels are
# hard to tell from black, and a logo drawn for a colour screen is dim in grey
LOGO_GAMMA = 0.85
# the pixels kept clear round each (left and right, top and bottom)
LOGO_MARGIN = (1, 1)
ICON_MARGIN = (2, 2)
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
# the framework's banner, while a core's list of games is up (Danik, 2026-10-26)
FRAMEWORK = os.path.join(ROOT, "assets", "oled_framework.png")
FRAMEWORK_GAP = 12               # between the text and the icon, as make_banner.py


def die(msg):
    print("make_oled: " + msg, file=sys.stderr)
    sys.exit(1)


def data(image):
    """The pixels of `image`, a list (getdata() is deprecated in newer Pillow)."""
    return list(image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata())


def levels(image):
    """`image` (L) in the panel's 16 levels, each to the nearest."""
    return image.point(lambda v: min(15, (v + 8) // 17) * 17)


def in_levels(image):
    return all(v % 17 == 0 for v in data(image))


def flatten(image, crop):
    """`image` cropped (to `crop`, else to what is not transparent), on black,
    in grey."""
    image = image.convert("RGBA")
    if crop is not None:
        x, y, w, h = crop
        image = image.crop((x, y, x + w, y + h))
    else:
        box = image.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
        if box is None:
            die("the picture is empty")
        image = image.crop(box)
    black = Image.new("RGBA", image.size, (0, 0, 0, 255))
    black.alpha_composite(image)
    grey = black.convert("L")
    if crop is None:
        # an opaque picture on black: to what is not black
        box = grey.point(lambda v: 255 if v >= 9 else 0).getbbox()
        if box is not None:
            grey = grey.crop(box)
    return grey


def fit(grey, width, height, stretch, gamma, margin=(0, 0)):
    """`grey` fitted into width x height (`margin` pixels off the left and
    right, and the top and bottom edges), centred on black, in the panel's
    levels."""
    mh, mv = margin
    if grey.size != (width - 2 * mh, height - 2 * mv):
        scale = min((width - 2 * mh) / grey.width, (height - 2 * mv) / grey.height)
        size = (max(1, round(grey.width * scale)), max(1, round(grey.height * scale)))
        grey = grey.resize(size, Image.LANCZOS)
    if stretch:
        grey = ImageOps.autocontrast(grey, cutoff=0.5)
    if gamma != 1:
        grey = grey.point(lambda v: round(255 * (v / 255) ** gamma))
    out = Image.new("L", (width, height), 0)
    out.paste(grey, ((width - grey.width) // 2, (height - grey.height) // 2))
    return levels(out)


def make(kind, src, crop=None, stretch=True, margin=None):
    width, height = (LOGO_W, LOGO_H) if kind == "logo" else (ICON_W, ICON_H)
    image = Image.open(src)
    if crop is None and margin is None and image.size == (width, height):
        grey = flatten(image, (0, 0, width, height))
        if in_levels(grey):
            return grey  # drawn for the panel: as it is
    grey = flatten(image, crop)
    if in_levels(grey):
        # drawn for the panel, fitted again: its greys stay as they are
        stretch, gamma = False, 1
    else:
        gamma = LOGO_GAMMA if kind == "logo" else 1
    if margin is None:
        margin = LOGO_MARGIN if kind == "logo" else ICON_MARGIN
    return fit(grey, width, height, stretch, gamma, margin)


def write_gsc(image, path):
    hexes = "".join("%x" % (v // 17) for v in data(levels(image)))
    with open(path, "w") as f:
        f.write("#define icon_width %d\n#define icon_height %d\nstatic unsigned char icon_bits[] = {\n"
                % image.size)
        for i in range(0, len(hexes), image.width):
            f.write(hexes[i:i + image.width] + "\n")


def read_gsc(path):
    head = open(path).read().split("\n", 3)
    width = int(re.search(r"width\s+(\d+)", head[0]).group(1))
    height = int(re.search(r"height\s+(\d+)", head[1]).group(1))
    body = head[3]
    if "0X" in body or "0x" in body:
        # the artwork pack's spelling, bytes of two pixels: 0X1f,0Xa2,
        body = "".join(re.findall(r"0[xX]([0-9a-fA-F]{2})", body))
    else:
        body = re.sub(r"[^0-9a-fA-F]", "", body)
    if len(body) < width * height:
        die("%s: %d pixels of %d" % (path, len(body), width * height))
    image = Image.new("L", (width, height))
    image.putdata([int(c, 16) * 17 for c in body[:width * height]])
    return image


def save(image, path):
    if path.lower().endswith(".gsc"):
        write_gsc(image, path)
    else:
        image.save(path)


def ascii_text(text):
    """`text` in printable ASCII: accents dropped, typographic marks plain."""
    text = text.translate({0x2018: "'", 0x2019: "'", 0x201C: '"', 0x201D: '"', 0x2013: "-", 0x2014: "-",
                           0x2026: "...", 0xDF: "ss", 0xC6: "AE", 0xE6: "ae", 0x152: "OE", 0x153: "oe"})
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return "".join(c for c in text if " " <= c <= "~")


def table(path, fields):
    """The lines of a games.txt or info.txt: {id: [fields]}, in order."""
    rows = {}
    if not os.path.exists(path):
        return rows
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\r\n")
        if line.startswith("#") or "|" not in line:
            continue
        parts = line.split("|", fields - 1)
        rows[parts[0]] = parts[1:]
    return rows


def sources(package, gid):
    """The logo's and the icon's source for game `gid`: (logo, icon); None where
    there is none."""
    own = os.path.join(package, "oled", gid + "_logo.png")
    logo = own if os.path.exists(own) else os.path.join(package, "logos", gid + ".png")
    icon = os.path.join(package, "oled", gid + "_icon.png")
    return (logo if os.path.exists(logo) else None, icon if os.path.exists(icon) else None)


def package_cmd(package, out):
    games = table(os.path.join(package, "games.txt"), 2)
    info = table(os.path.join(package, "info.txt"), 4)
    if not games:
        die("%s/games.txt has no games" % package)
    missing = []
    for gid in games:
        logo, icon = sources(package, gid)
        if logo is None:
            missing.append("package/logos/%s.png" % gid)
        if icon is None:
            missing.append("package/oled/%s_icon.png (86x64)" % gid)
        if gid not in info:
            missing.append("the line %s|<year>|<developer>|<description> of package/info.txt" % gid)
    if missing:
        die("every game has its OLED logo, icon and info (hybrid/docs/oled.md); missing:\n  "
            + "\n  ".join(missing))
    os.makedirs(os.path.join(out, "oled"), exist_ok=True)
    lines = ["# The games' details for tty2oled+'s display (hybrid/docs/oled.md):",
             "# <id>|<year>|<developer>|<description>, printable ASCII"]
    for gid in games:
        logo, icon = sources(package, gid)
        write_gsc(make("logo", logo), os.path.join(out, "oled", gid + "_logo.gsc"))
        write_gsc(make("icon", icon), os.path.join(out, "oled", gid + "_icon.gsc"))
        lines.append("|".join([gid] + [ascii_text(f) for f in info[gid]]))
    with open(os.path.join(out, "info.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    write_gsc(make("logo", FRAMEWORK), os.path.join(out, "oled", "framework_logo.gsc"))


def framework_cmd(out):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from make_banner import FRAMEWORK_TEXT, cozette_text
    text = cozette_text(FRAMEWORK_TEXT)
    icon = Image.open(os.path.join(ROOT, "assets", "danik.png")).convert("RGBA")
    x = (LOGO_W - text.width - FRAMEWORK_GAP - icon.width) // 2
    image = Image.new("RGBA", (LOGO_W, LOGO_H), (0, 0, 0, 255))
    image.paste(Image.new("RGBA", text.size, (255, 255, 255, 255)), (x, (LOGO_H - text.height) // 2), text)
    image.alpha_composite(icon, (x + text.width + FRAMEWORK_GAP, (LOGO_H - icon.height) // 2))
    save(levels(image.convert("L")), out)


def font(size):
    from PIL import ImageFont
    for name in ("DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                 "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def preview_cmd(package, out, scale):
    """The logo, and the icon beside the title, year and developer, as two
    screens of the panel, a game a row; transparent-free, a gap between."""
    games = table(os.path.join(package, "games.txt"), 2)
    info = table(os.path.join(package, "info.txt"), 4)
    small = font(10)
    rows = []
    for gid, (title,) in games.items():
        logo, icon = sources(package, gid)
        a = make("logo", logo) if logo else Image.new("L", (LOGO_W, LOGO_H), 0)
        b = Image.new("L", (LOGO_W, LOGO_H), 0)
        if icon:
            b.paste(make("icon", icon), (LOGO_W - ICON_W, 0))
        d = ImageDraw.Draw(b)
        year, dev = (info[gid][0], info[gid][1]) if gid in info else ("", "")
        for i, text in enumerate(["Now playing", title, "Dev: " + dev, "Year: " + year, gid]):
            d.text((2, 1 + i * 12), text, fill=255 if i == 1 else 170, font=small)
        rows.append((a, levels(b)))
    gap = 4
    sheet = Image.new("L", (LOGO_W * 2 + gap * 3, (LOGO_H + gap) * len(rows) + gap), 48)
    for i, (a, b) in enumerate(rows):
        y = gap + i * (LOGO_H + gap)
        sheet.paste(a, (gap, y))
        sheet.paste(b, (LOGO_W + gap * 2, y))
    sheet.resize((sheet.width * scale, sheet.height * scale), Image.NEAREST).save(out)


def crop_arg(args):
    if "--crop" not in args:
        return None
    i = args.index("--crop")
    value = args[i + 1]
    del args[i:i + 2]
    return tuple(int(v) for v in value.split(","))


def margin_arg(args):
    if "--margin" not in args:
        return None
    i = args.index("--margin")
    value = [int(v) for v in args[i + 1].split(",")]
    del args[i:i + 2]
    return (value[0], value[-1])


def main(argv):
    args = argv[1:]
    if not args:
        die("usage: see the header of this file")
    cmd = args.pop(0)
    crop = crop_arg(args)
    margin = margin_arg(args)
    stretch = "--no-stretch" not in args
    args = [a for a in args if a != "--no-stretch"]
    if cmd in ("logo", "icon") and len(args) == 2:
        save(make(cmd, args[0], crop, stretch, margin), args[1])
    elif cmd == "gsc2png" and len(args) == 2:
        read_gsc(args[0]).save(args[1])
    elif cmd == "framework" and len(args) == 1:
        framework_cmd(args[0])
    elif cmd == "package" and len(args) == 2:
        package_cmd(args[0], args[1])
    elif cmd == "preview" and len(args) >= 2:
        scale = int(args[args.index("--scale") + 1]) if "--scale" in args else 2
        preview_cmd(args[0], args[1], scale)
    else:
        die("usage: see the header of this file")


if __name__ == "__main__":
    main(sys.argv)
