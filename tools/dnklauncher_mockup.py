#!/usr/bin/env python3
# Mock-ups of the screens of dnkLauncher, the frontend core (docs/dnklauncher.md,
# phase 1): what its program would draw, 640x480 (interlaced on a 15kHz screen,
# progressive on HDMI and 31kHz, as the core's own purple screen), with the
# logos and games of the published ports. Nothing here runs on the MiSTer; the
# sizes and looks it settles go into the program and into the pictures
# distribution.py will make.
#
#   the frame    the MiSTer logo (assets/MiSTer.png) at the top, the danik badge
#                (assets/danik.png, "Hybrid Core Framework vN" / "by danik" in
#                Cozette, as hybrid_host.sv draws it) at the bottom right
#   the text     a font of tools/dnklauncher_fonts.py's comparison (FONTS)
#   the colours  the core's purple (WALL_BG of hybrid_host.sv) as a gradient,
#                the pink of "FPGA", and a few more (below)
#
# Per game: its logo (package/logos/<id>.png of its port), its screenshot
# (package/screens/<id>.png, or the port's docs/screenshot2.png while it has
# none) and its year, developer and description (package/info.txt; INFO below
# for a port without one).
#
# usage: dnklauncher_mockup.py <fonts dir> <out dir> [repositories...]
#   the fonts dir: tools/dnklauncher_fonts.py's; the repositories: the ports'
#   (MiSTer-*), without them every ~/MiSTer-* of a port the database at
#   ~/dnk_Distribution_MiSTer has. Writes <out>/<font>_<screen>.png, and
#   the animations, a frame per field, as <out>/anim_<name>.webp and, a
#   quarter as fast, anim_<name>_slow.webp.
import glob
import os
import re
import sys

from PIL import BdfFontFile, Image, ImageChops, ImageDraw, ImageFilter, ImageFont

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

W, H = 640, 480
BG_TOP = (0x8C, 0x3B, 0xB1)     # around WALL_BG (7F30A0), lighter at the top
BG_BOTTOM = (0x58, 0x1E, 0x73)  # and darker at the bottom
PANEL = (0x3E, 0x12, 0x55)      # the panels' ground, laid over the gradient
PANEL_ALPHA = 205
EDGE = (0xC3, 0x84, 0xE2)       # the panels' rim
INK = (0xF6, 0xF0, 0xFA)
DIM = (0xBC, 0xA2, 0xCE)
PINK = (0xF2, 0x8F, 0xBF)       # "FPGA" of the MiSTer logo
PEACH = (0xFF, 0xBE, 0x8A)      # the cursor's bar runs from PINK to PEACH
SEL_INK = (0x36, 0x0C, 0x4A)
GOLD = (0xFF, 0xD1, 0x66)       # a favourite's star
CYAN = (0x7A, 0xDB, 0xEC)       # ports, years, the version
CORAL = (0xFF, 0x7A, 0x63)      # no data
VERSION = 17                    # of hybrid_host.sv, in the badge


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


# The colour schemes, the player's choice in Options (Danik, 2026-10-26: "I
# like all of the colour schemes, include all of them"; MiSTer Purple the
# default). Each names every colour above, its bar too ("the bar should remain
# the default colour for each colour scheme"): "bar" is PINK to PEACH, the
# cursor's bar and the sort mode's pill, "sel" the ink on it. apply_theme()
# makes one the colours of what is drawn.
THEMES = {
    "purple": dict(label="MiSTer Purple", bg=("8C3BB1", "581E73"), panel=("3E1255", 205), edge="C384E2",
                   ink="F6F0FA", dim="BCA2CE", bar=("F28FBF", "FFBE8A"), sel="360C4A", gold="FFD166",
                   accent="7ADBEC", alert="FF7A63"),
    "midnight": dict(label="Midnight", bg=("27306E", "0B0F2E"), panel=("0E1440", 210), edge="7C93F0",
                     ink="EEF2FF", dim="A0ABD6", bar=("6FE3F7", "8C9BFF"), sel="0A1236", gold="FFD166",
                     accent="8EF0CE", alert="FF7F7F"),
    "synthwave": dict(label="Synthwave", bg=("3A0E52", "0D0418"), panel=("1C0630", 215), edge="FF5FA2",
                      ink="FDEBFF", dim="BC95CF", bar=("FF3D8B", "FFA445"), sel="2A0420", gold="FFE45E",
                      accent="45E9FF", alert="FF6B6B"),
    "arcade": dict(label="Arcade", bg=("34343C", "0E0E12"), panel=("16161B", 215), edge="8A8A99",
                   ink="F5F5F5", dim="ABABB6", bar=("FFD23F", "FF8A00"), sel="2A1400", gold="FFD23F",
                   accent="5FC9FF", alert="FF5A4E"),
    "amber": dict(label="Amber Phosphor", bg=("33200A", "0E0802"), panel=("1A1004", 215), edge="B87A1E",
                  ink="FFD892", dim="C29148", bar=("FFB000", "FFD060"), sel="2A1800", gold="FFE7A0",
                  accent="FFC56B", alert="FF6A3D"),
    "sunset": dict(label="Sunset", bg=("C04E62", "45174A"), panel=("3A1238", 205), edge="FFB38A",
                   ink="FFF4EC", dim="E6BBBC", bar=("FFD27A", "FF8C69"), sel="4A1220", gold="FFE27A",
                   accent="A2EBE3", alert="FF6F59"),
    "ocean": dict(label="Ocean", bg=("13697A", "06222E"), panel=("06303B", 210), edge="7FD6CF",
                  ink="EFFFFD", dim="9DCAC6", bar=("7FF0C8", "D5F27F"), sel="06302A", gold="FFD166",
                  accent="FFB38A", alert="FF7A63"),
    "lavender": dict(label="Lavender (light)", bg=("F2E9F9", "D6C1EA"), panel=("FFFFFF", 190), edge="A574C8",
                     ink="2A1238", dim="6E5A80", bar=("8A35B0", "C051D6"), sel="FFFFFF", gold="E0A000",
                     accent="1F7A99", alert="D9442F"),
}


def apply_theme(name):
    global BG_TOP, BG_BOTTOM, PANEL, PANEL_ALPHA, EDGE, INK, DIM, PINK, PEACH, SEL_INK, GOLD, CYAN, CORAL
    t = THEMES[name]
    BG_TOP, BG_BOTTOM = rgb(t["bg"][0]), rgb(t["bg"][1])
    PANEL, PANEL_ALPHA = rgb(t["panel"][0]), t["panel"][1]
    EDGE, INK, DIM, SEL_INK = rgb(t["edge"]), rgb(t["ink"]), rgb(t["dim"]), rgb(t["sel"])
    PINK, PEACH = rgb(t["bar"][0]), rgb(t["bar"][1])
    GOLD, CYAN, CORAL = rgb(t["gold"]), rgb(t["accent"]), rgb(t["alert"])

M = 30                          # from the sides; a 15kHz screen hides some of them

# The player's choice in Options (Danik, 2026-10-25: Chakra Petch the default;
# their places in tools/dnklauncher_fonts.py's ranking by flicker after them)
SIZES = dict(row=16, text=15, title=20, small=12, chip=13, head=18, desc=14)
FONTS = {   # key: (label, file in the fonts dir, weight of a variable font, sizes)
    "chakra": ("Chakra Petch SemiBold", "ChakraPetch-SemiBold.ttf", None, SIZES),       # 5
    "nunito": ("Nunito ExtraBold", "Nunito.ttf", 800, SIZES),                           # 1
    "atkinson": ("Atkinson Hyperlegible Bold", "Atkinson-Bold.ttf", None, SIZES),       # 3
    "rubik": ("Rubik Medium", "Rubik.ttf", 500, SIZES),                                 # 6
    "lexend": ("Lexend SemiBold", "Lexend.ttf", 600, SIZES),                            # 10
}

# Button symbols, by the button's place (Linux names a pad's buttons so:
# BTN_SOUTH is the bottom one, whatever is printed on it). A family: for each
# place what is printed there (a letter, or a PlayStation shape), its ink and
# its ground, and the names of Select and Start. "snes" is MiSTer's own naming
# (A east, B south, X north, Y west), for a controller of no known family.
FAMILIES = {
    "switch": ("Nintendo Switch", {
        "south": ("B", (0xF0, 0xF0, 0xF0), (0x2E, 0x2E, 0x33)), "east": ("A", (0xF0, 0xF0, 0xF0), (0x2E, 0x2E, 0x33)),
        "north": ("X", (0xF0, 0xF0, 0xF0), (0x2E, 0x2E, 0x33)), "west": ("Y", (0xF0, 0xF0, 0xF0), (0x2E, 0x2E, 0x33)),
        "select": ("-", None, None), "start": ("+", None, None)}),
    "xbox": ("Xbox", {
        "south": ("A", (0x6C, 0xC6, 0x2E), (0x22, 0x22, 0x26)), "east": ("B", (0xEE, 0x4B, 0x4B), (0x22, 0x22, 0x26)),
        "north": ("Y", (0xF5, 0xC4, 0x1A), (0x22, 0x22, 0x26)), "west": ("X", (0x3F, 0x8F, 0xEF), (0x22, 0x22, 0x26)),
        "select": ("VIEW", None, None), "start": ("MENU", None, None)}),
    "ps": ("PlayStation", {
        "south": ("cross", (0x8F, 0xB4, 0xF2), (0x22, 0x22, 0x2A)), "east": ("circle", (0xF2, 0x6B, 0x75), (0x22, 0x22, 0x2A)),
        "north": ("triangle", (0x4F, 0xD1, 0xB0), (0x22, 0x22, 0x2A)), "west": ("square", (0xEE, 0x9C, 0xDC), (0x22, 0x22, 0x2A)),
        "select": ("CREATE", None, None), "start": ("OPTIONS", None, None)}),
    "snes": ("SNES (MiSTer's names)", {
        "south": ("B", (0xFF, 0xFF, 0xFF), (0xF5, 0xC5, 0x42)), "east": ("A", (0xFF, 0xFF, 0xFF), (0xE8, 0x4A, 0x5F)),
        "north": ("X", (0xFF, 0xFF, 0xFF), (0x4A, 0x90, 0xE2)), "west": ("Y", (0xFF, 0xFF, 0xFF), (0x5C, 0xC2, 0x6A)),
        "select": ("SELECT", None, None), "start": ("START", None, None)}),
}
# Where the frontend's actions are: by letter (Danik, 2026-10-25), the same
# letter on every pad: Play A, Exit B, Favourite Y, Ports X (PlayStation:
# Cross, Circle, Triangle, Square in Xbox's places). A Switch has Xbox's
# layout mirrored across the diagonal (A east, B south, X north, Y west), so
# there all four are mirrored, as Menu OK / Back are in the MiSTer menu
# (Nintendo's confirms with the east button, convention_codes()). Play and
# Exit are Menu OK and Menu Back: a controller with a mapping of its own in
# the MiSTer menu has them where the mapping says.
def places(family):
    if family == "switch":
        return {"ok": "east", "back": "south", "fav": "west", "ports": "north", "sort": "select", "options": "start"}
    return {"ok": "south", "back": "east", "fav": "north", "ports": "west", "sort": "select", "options": "start"}


class Fonts:
    def __init__(self, fonts_dir, key):
        label, path, weight, sizes = FONTS[key]
        self.label = label
        for role, size in sizes.items():
            f = ImageFont.truetype(os.path.join(fonts_dir, path), size, layout_engine=ImageFont.Layout.RAQM)
            if weight:
                f.set_variation_by_axes([weight])
            setattr(self, role, f)


def cozette(out):
    base = os.path.join(out, "_cozette")
    if not os.path.exists(base + ".pil"):
        with open(os.path.join(root, "assets", "cozette.bdf"), "rb") as fp:
            BdfFontFile.BdfFontFile(fp).save(base)
    return ImageFont.load(base + ".pil")


class Canvas:
    def __init__(self, ground=True, size=(W, H)):
        self.im = Image.new("RGBA", size, (0, 0, 0, 0))
        if ground:
            d = ImageDraw.Draw(self.im)
            for y in range(size[1]):
                t = y / (size[1] - 1)
                d.line((0, y, size[0], y), fill=tuple(round(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM)) + (255,))

    def layer(self):
        return Image.new("RGBA", self.im.size, (0, 0, 0, 0))

    def over(self, layer):
        self.im.alpha_composite(layer)

    def panel(self, box, radius=10):
        x0, y0, x1, y1 = box
        shadow = self.layer()
        ImageDraw.Draw(shadow).rounded_rectangle((x0 + 2, y0 + 5, x1 + 2, y1 + 5), radius, fill=(20, 0, 30, 120))
        self.over(shadow.filter(ImageFilter.GaussianBlur(5)))
        body = self.layer()
        d = ImageDraw.Draw(body)
        d.rounded_rectangle(box, radius, fill=PANEL + (PANEL_ALPHA,), outline=EDGE + (150,), width=1)
        d.line((x0 + radius, y0 + 1, x1 - radius, y0 + 1), fill=(255, 255, 255, 50))   # light from above
        self.over(body)

    def text(self, xy, s, font, colour, anchor="la"):
        layer = self.layer()
        ImageDraw.Draw(layer).text(xy, s, font=font, fill=colour + (255,), anchor=anchor)
        self.over(layer)

    def bar(self, box, radius=7):
        """The cursor's bar: PINK to PEACH, rounded"""
        x0, y0, x1, y1 = box
        grad = Image.new("RGBA", (x1 - x0, y1 - y0))
        gd = ImageDraw.Draw(grad)
        for x in range(grad.width):
            t = x / max(1, grad.width - 1)
            gd.line((x, 0, x, grad.height), fill=tuple(round(a + (b - a) * t) for a, b in zip(PINK, PEACH)) + (255,))
        mask = Image.new("L", grad.size, 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, grad.width - 1, grad.height - 1), radius, fill=255)
        layer = self.layer()
        layer.paste(grad, (x0, y0), mask)
        self.over(layer)

    def pill(self, xy, s, font, ground, ink, anchor_right=False, pad=6, h=None):
        w = round(font.getlength(s)) + 2 * pad
        h = h or font.size + 6
        x, y = xy
        if anchor_right:
            x -= w
        layer = self.layer()
        d = ImageDraw.Draw(layer)
        d.rounded_rectangle((x, y, x + w, y + h), h // 2, fill=ground + (255,))
        d.text((x + w / 2, y + h / 2), s, font=font, fill=ink + (255,), anchor="mm")
        self.over(layer)
        return w

    def star(self, cx, cy, r, colour):
        import math
        pts = []
        for i in range(10):
            a = -math.pi / 2 + i * math.pi / 5
            rr = r if i % 2 == 0 else r * 0.45
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        layer = self.layer()
        ImageDraw.Draw(layer).polygon(pts, fill=colour + (255,))
        self.over(layer)

    def picture(self, im, xy):
        self.im.alpha_composite(im.convert("RGBA"), xy)


def clip(s, font, width):
    if font.getlength(s) <= width:
        return s
    while s and font.getlength(s + "...") > width:
        s = s[:-1]
    return s.rstrip() + "..."


def wrap(text, font, width):
    lines, line = [], ""
    for word in text.split():
        t = (line + " " + word).strip()
        if line and font.getlength(t) > width:
            lines.append(line)
            line = word
        else:
            line = t
    return lines + [line] if line else lines


def to_linear_resize(im, size):
    """Scaled in linear light, with Lanczos: what distribution.py will do once"""
    lin = im.point([round(((v / 255) ** 2.2) * 255) for v in range(256)] * len(im.getbands()))
    small = lin.resize(size, Image.LANCZOS)
    return small.point([round(((v / 255) ** (1 / 2.2)) * 255) for v in range(256)] * len(im.getbands()))


def logo_picture(path, box_w, box_h):
    logo = Image.open(path).convert("RGBA")
    logo = logo.crop(logo.getbbox())
    s = min(box_w / logo.width, box_h / logo.height)
    return logo.resize((max(1, round(logo.width * s)), max(1, round(logo.height * s))), Image.LANCZOS)


def shot_picture(path, w):
    return to_linear_resize(Image.open(path).convert("RGB"), (w, round(w * 3 / 4)))


# What the mock-ups show of the games.

INFO = {   # a game's year, developer and description; the main screen shows none of them, Details all
    "doom2": ("1994", "id Software",
              "The invasion has reached Earth. The one marine who made it back from Mars fights through "
              "starports, a ruined city and finally Hell itself, to close the portal from the other side. "
              "Thirty-two levels, the super shotgun and new demons such as the Arch-vile made it the "
              "sequel every shooter after it was measured against."),
    "Q2": ("1997", "id Software",
           "Humanity strikes back on the Strogg's own world: a marine lands alone and "
           "fights through their bases, factories and prisons to the Makron."),
    "DN3D": ("1996", "3D Realms",
             "Aliens have landed in Los Angeles, and they picked the wrong city. Duke kicks, shoots and "
             "wisecracks his way through cinemas, shops, a space station and the Moon, in levels full "
             "of things to break, switch on and climb into."),
    "master": ("1995", "id Software and others",
               "Twenty-one levels by designers from outside id, chosen for the Master Levels "
               "collection: each one a map of its own, from tight arenas to sprawling bases."),
    "nerve": ("2010", "Nerve Software",
              "Nine new levels made for the Xbox 360 release of Doom II, picking up where its "
              "story ends, with a secret level of their own."),
    "hexen": ("1995", "Raven Software",
              "Korax, the second Serpent Rider, has taken Cronos. Three heroes stand against him: a "
              "fighter with fists and an axe, a cleric with a mace and a serpent staff, and a mage who "
              "throws lightning, through hubs of maps linked to each other, with switches to find and "
              "puzzles that span them."),
}
FAVOURITES = {"doom2", "QUAKE", "DN3D"}
NO_DATA = {"hexen", "hexdd", "strife", "S2"}
RECENT = [("DN3D", "today"), ("doom2", "today"), ("Q2", "yesterday"), ("WL", "3 days ago"),
          ("CARMA", "last week"), ("HL", "2 weeks ago"), ("SE", "a month ago")]
INSTALLED = {"CrispyDoom", "ChocolateQuake", "EDuke32", "ECWolf", "YQuake2", "Xash3D", "Dethrace", "ReNFS"}


def load_ports(repos):
    ports = []
    db = os.path.expanduser("~/dnk_Distribution_MiSTer/ports.txt")
    versions = {}
    if os.path.exists(db):
        for line in open(db):
            if "|" in line and not line.startswith("#"):
                engine, z = line.strip().split("|")
                versions[engine] = z.rsplit("_", 1)[1].split(".")[0]
    for repo in repos:
        qpf = glob.glob(os.path.join(repo, "core", "*.qpf"))
        games_txt = os.path.join(repo, "package", "games.txt")
        if not qpf or not os.path.exists(games_txt):
            continue
        engine = os.path.basename(qpf[0])[:-4]
        if versions and engine not in versions:
            continue
        games = []
        for line in open(games_txt):
            if "|" in line and not line.startswith("#"):
                gid, title = line.strip().split("|", 1)
                shot = os.path.join(repo, "package", "screens", gid + ".png")
                if not os.path.exists(shot):
                    shot = os.path.join(repo, "docs", "screenshot2.png")
                games.append({"id": gid, "title": title, "engine": engine,
                              "logo": os.path.join(repo, "package", "logos", gid + ".png"), "shot": shot})
        info = os.path.join(repo, "package", "info.txt")
        if os.path.exists(info):
            for line in open(info, encoding="utf-8"):
                if "|" in line and not line.startswith("#"):
                    gid, year, dev, desc = line.rstrip("\n").split("|", 3)
                    INFO[gid] = (year, dev, desc)
        ports.append({"engine": engine, "version": versions.get(engine, "?"), "games": games})
    return ports


# The layout. No strip of text under the list: the main screen is the list
# and the artwork, and A opens the game's Details page with everything else.

HEAD = (19, 75)           # the MiSTer logo and the sort modes
MAIN = (83, 393)          # the list and the preview; Details' panels
FOOT = (405, 441)         # the buttons and the badge
LIST_X = (M, 330)
PREV_X = (342, W - M)
ROW = 24
N_ROWS = (MAIN[1] - MAIN[0] - 12) // ROW
LIST_PAD = (MAIN[1] - MAIN[0] - N_ROWS * ROW) // 2
SHOT = (256, 192)         # the screenshot, in the preview and in Details
LOGO_H = 64               # the game's logo in the preview
WALL_BG = (0x7F, 0x30, 0xA0)   # the core's own screen (hybrid_host.sv): its ground,
WALL_LOGO = (105, 184)         # where its logo (429x111) is, WALL_LOGO_X / _Y,
WALL_EDGE = 34                 # and its badge, 34 from the right edge and the bottom
FIELD_MS = 1000 / 59.64
CHIPS = ["A-Z", "By port", "Recent", "Favourites"]

# Details: the screenshot top left, the logo and the facts top right, the
# description across the bottom
D_SHOT_PANEL = (M, MAIN[0], M + SHOT[0] + 16, MAIN[0] + SHOT[1] + 16)
D_INFO_PANEL = (D_SHOT_PANEL[2] + 12, MAIN[0], W - M, D_SHOT_PANEL[3])
D_DESC_PANEL = (M, D_SHOT_PANEL[3] + 10, W - M, MAIN[1])
D_SHOT = (D_SHOT_PANEL[0] + 8, D_SHOT_PANEL[1] + 8)


def button(c, F, family, place, x, y):
    """The symbol of the button at that place on a controller of the family,
    18 high, its left edge at x; returns its width"""
    sym, ink, ground = FAMILIES[family][1][place]
    if ink is None:     # Select and Start: their names in a grey pill
        if sym in "+-":
            layer = c.layer()
            d = ImageDraw.Draw(layer)
            d.ellipse((x, y, x + 18, y + 18), fill=(0x2E, 0x2E, 0x33, 255))
            d.line((x + 5, y + 9, x + 13, y + 9), fill=(240, 240, 240, 255), width=2)
            if sym == "+":
                d.line((x + 9, y + 5, x + 9, y + 13), fill=(240, 240, 240, 255), width=2)
            c.over(layer)
            return 18
        return c.pill((x, y), sym, F.small, (0x8E, 0x80, 0x9C), (255, 255, 255), pad=4, h=18)
    layer = c.layer()
    d = ImageDraw.Draw(layer)
    d.ellipse((x, y, x + 18, y + 18), fill=ground + (255,))
    cx, cy = x + 9, y + 9
    if sym == "cross":
        d.line((cx - 4, cy - 4, cx + 4, cy + 4), fill=ink + (255,), width=2)
        d.line((cx - 4, cy + 4, cx + 4, cy - 4), fill=ink + (255,), width=2)
    elif sym == "circle":
        d.ellipse((cx - 5, cy - 5, cx + 5, cy + 5), outline=ink + (255,), width=2)
    elif sym == "square":
        d.rectangle((cx - 4, cy - 4, cx + 4, cy + 4), outline=ink + (255,), width=2)
    elif sym == "triangle":
        d.polygon((cx, cy - 5, cx + 5.5, cy + 4, cx - 5.5, cy + 4), outline=ink + (255,), width=2)
    else:
        d.text((cx, cy + 0.5), sym, font=F.small, fill=ink + (255,), anchor="mm")
    c.over(layer)
    return 18


# Layers: each part of a screen drawn on its own, transparent elsewhere, so
# that the animations can move and fade it. The program would keep them so too
# and only put them together, once per field.

def blank():
    return Image.new("RGBA", (W, H), (0, 0, 0, 0))


def fade(im, alpha):
    if alpha >= 1:
        return im
    out = im.copy()
    out.putalpha(out.getchannel("A").point(lambda v: round(v * alpha)))
    return out


def place(layer, dx=0.0, dy=0.0, alpha=1.0):
    """The layer moved and faded"""
    if alpha <= 0:
        return blank()
    out = layer
    if round(dx) or round(dy):
        out = blank()
        out.paste(layer, (round(dx), round(dy)))
    return fade(out, alpha)


def clip_to(layer, box):
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rectangle(box, fill=255)
    out = layer.copy()
    out.putalpha(ImageChops.multiply(out.getchannel("A"), mask))
    return out


def ease_out(t):
    t = min(1.0, max(0.0, t))
    return 1 - (1 - t) ** 3


def ease_in_out(t):
    t = min(1.0, max(0.0, t))
    return 4 * t ** 3 if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2


def span(f, a, b):
    """How far field f is from a to b, 0..1"""
    return min(1.0, max(0.0, (f - a) / (b - a)))


def lerp(a, b, t):
    return a + (b - a) * t


_cache = {}


def mister_logo(scale):
    key = ("logo", round(scale, 3))
    if key not in _cache:
        logo = Image.open(os.path.join(root, "assets", "MiSTer.png")).convert("RGBA")
        size = (max(1, round(logo.width * scale)), max(1, round(logo.height * scale)))
        _cache[key] = logo.resize(size, Image.LANCZOS)
    return _cache[key]


def layer_ground():
    return Canvas().im


def layer_logo(scale=0.5, xy=(M - 4, HEAD[0])):
    out = blank()
    out.paste(mister_logo(scale), (round(xy[0]), round(xy[1])))
    return out


def layer_title(F):
    c = Canvas(ground=False)
    c.text((W - M, HEAD[0] + 2), "Hybrid Ports", F.title, INK, anchor="ra")
    return c.im


def layer_count(F, count):
    c = Canvas(ground=False)
    c.text((W - M - F.title.getlength("Hybrid Ports") - 10, HEAD[0] + 7), count, F.small, DIM, anchor="ra")
    return c.im


def chip_rects(F):
    rects, x = {}, W - M
    for m in reversed(CHIPS):
        w = round(F.chip.getlength(m)) + 16
        x -= w
        rects[m] = (x, w)
        x -= 6
    return rects


def layer_chips(F, family, on):
    """The sort modes, the pink pill at on = (x, width): a chip's, or between two while it slides"""
    rects = chip_rects(F)
    c = Canvas(ground=False)
    sel = FAMILIES[family][1]["select"][0]
    sw = 18 if sel in "+-" else round(F.small.getlength(sel)) + 8
    left = min(x for x, w in rects.values())
    button(c, F, family, places(family)["sort"], left - 6 - sw, HEAD[0] + 33)
    y0, y1 = HEAD[0] + 32, HEAD[0] + 52
    layer = c.layer()
    d = ImageDraw.Draw(layer)
    for m, (x, w) in rects.items():
        d.rounded_rectangle((x, y0, x + w, y1), 10, fill=PANEL + (150,), outline=EDGE + (120,))
        d.text((x + w / 2, y0 + 10), m, font=F.chip, fill=DIM + (255,), anchor="mm")
    c.over(layer)
    ox, ow = on
    pill = blank()
    ImageDraw.Draw(pill).rounded_rectangle((ox, y0, ox + ow, y1), 10, fill=PINK + (255,))
    c.over(pill)
    ink = blank()
    d = ImageDraw.Draw(ink)
    for m, (x, w) in rects.items():
        d.text((x + w / 2, y0 + 10), m, font=F.chip, fill=SEL_INK + (255,), anchor="mm")
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rounded_rectangle((ox, y0, ox + ow, y1), 10, fill=255)
    ink.putalpha(ImageChops.multiply(ink.getchannel("A"), mask))
    c.over(ink)
    return c.im


def layer_panel(box, radius=10):
    c = Canvas(ground=False)
    c.panel(box, radius)
    return c.im


def list_strips(F, rows):
    """The rows one under the other, as they are and as they are under the cursor's bar"""
    w = LIST_X[1] - LIST_X[0]
    out = []
    for sel in (False, True):
        c = Canvas(ground=False, size=(w, max(1, len(rows)) * ROW))
        for n, (kind, label, game, tag) in enumerate(rows):
            y = n * ROW
            mid = y + ROW / 2
            if sel:
                c.bar((5, y + 1, w - 5, y + ROW - 1))
            if kind == "head":
                c.text((12, mid), label, F.chip, SEL_INK if sel else CYAN, anchor="lm")
                lw = F.chip.getlength(label)
                layer = c.layer()
                ImageDraw.Draw(layer).line((18 + lw, mid, w - 12, mid), fill=(SEL_INK if sel else CYAN) + (90,))
                c.over(layer)
                continue
            nodata = game["id"] in NO_DATA
            if game["id"] in FAVOURITES:
                c.star(16, mid, 6, SEL_INK if sel else GOLD)
            right = w - 12
            if nodata:
                right -= c.pill((right, mid - 8), "no data", F.small, SEL_INK if sel else CORAL,
                                (255, 255, 255), anchor_right=True, h=16) + 6
            elif tag:
                c.text((right, mid), tag, F.small, SEL_INK if sel else DIM, anchor="rm")
                right -= F.small.getlength(tag) + 8
            ink = SEL_INK if sel else (DIM if nodata else INK)
            c.text((28, mid), clip(label, F.row, right - 28), F.row, ink, anchor="lm")
        out.append(c.im)
    return out


def layer_rows(strips, n_total, scroll, bar, dx=0.0):
    """The rows from row `scroll` on and the cursor's bar on row `bar` (both
    fractions while they move): the rows under the bar as they are there"""
    normal, selected = strips
    x0, x1 = LIST_X
    w, h = x1 - x0, N_ROWS * ROW
    top = round(scroll * ROW)
    view = normal.crop((0, top, w, top + h))
    by = round((bar - scroll) * ROW)
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle((5, by + 1, w - 6, by + ROW - 2), 7, fill=255)
    view.paste(selected.crop((0, top, w, top + h)), (0, 0), mask)
    out = blank()
    out.paste(view, (x0 + round(dx), MAIN[0] + LIST_PAD))
    out = clip_to(out, (x0 + 2, MAIN[0] + 2, x1 - 6, MAIN[1] - 2))
    if n_total > N_ROWS:      # the scroll bar
        sh = MAIN[1] - MAIN[0] - 24
        knob = max(16, sh * N_ROWS // n_total)
        kt = round((sh - knob) * scroll / (n_total - N_ROWS))
        d = ImageDraw.Draw(out)
        d.rounded_rectangle((x1 - 4, MAIN[0] + 12, x1 - 1, MAIN[1] - 12), 2, fill=EDGE + (60,))
        d.rounded_rectangle((x1 - 4, MAIN[0] + 12 + kt, x1 - 1, MAIN[0] + 12 + kt + knob), 2, fill=EDGE + (230,))
    return out


def preview_shot_xy():
    pw = PREV_X[1] - PREV_X[0]
    top = MAIN[0] + (MAIN[1] - MAIN[0] - LOGO_H - 12 - SHOT[1]) // 2
    return PREV_X[0] + (pw - SHOT[0]) // 2, top + LOGO_H + 12


def layer_shot(F, game, xy):
    """The screenshot in its dark frame, or what is missing when the game's data is"""
    c = Canvas(ground=False)
    sw, sh = SHOT
    sx, sy = round(xy[0]), round(xy[1])
    layer = c.layer()
    ImageDraw.Draw(layer).rectangle((sx - 2, sy - 2, sx + sw + 1, sy + sh + 1), fill=(15, 0, 22, 255))
    c.over(layer)
    if game["id"] in NO_DATA:
        msg = "%s was not found. Copy HEXEN.WAD to games/dnk/%s/." % (game["title"], game["engine"])
        layer = c.layer()
        ImageDraw.Draw(layer).rectangle((sx, sy, sx + sw - 1, sy + sh - 1), fill=(0x2A, 0x0A, 0x3A, 255))
        c.over(layer)
        lines = wrap(msg, F.text, sw - 32)
        top = sy + (sh - 30 - len(lines) * 19) // 2
        c.pill((sx + sw // 2 - 50, top), "No game data", F.small, CORAL, (255, 255, 255), h=20)
        for n, line in enumerate(lines):
            c.text((sx + sw / 2, top + 32 + n * 19), line, F.text, INK, anchor="ma")
    else:
        key = ("shot", game["shot"])
        if key not in _cache:
            _cache[key] = shot_picture(game["shot"], sw)
        c.picture(_cache[key], (sx, sy))
    return c.im


def layer_preview_logo(game, dx=0.0):
    out = blank()
    pw = PREV_X[1] - PREV_X[0]
    logo = logo_picture(game["logo"], pw - 24, LOGO_H)
    sy = preview_shot_xy()[1]
    out.alpha_composite(logo, (PREV_X[0] + (pw - logo.width) // 2 + round(dx), sy - 12 - logo.height))
    return out


def layer_preview(F, game, logo_dx=0.0):
    out = layer_preview_logo(game, logo_dx)
    out.alpha_composite(layer_shot(F, game, preview_shot_xy()))
    return out


def layer_hints(F, hints, badge_font, family):
    """hints: (action, what it does); the action's button as the family prints it"""
    c = Canvas(ground=False)
    y = FOOT[0] + 9
    where = places(family)
    # the gaps shrink to keep the hints clear of the badge, whatever the font
    probe = Canvas(ground=False)
    width = sum(button(probe, F, family, where[a], 0, 0) + 4 + round(F.chip.getlength(w)) for a, w in hints)
    room = W - M - 36 - 8 - badge_font.getbbox("Hybrid Core Framework v%d" % VERSION)[2] - 12 - M
    gap = max(4, min(12, (room - width) // max(1, len(hints) - 1)))
    x = M
    for action, what in hints:
        x += button(c, F, family, where[action], x, y) + 4
        c.text((x, y + 9), what, F.chip, INK, anchor="lm")
        x += round(F.chip.getlength(what)) + gap
    return c.im


def badge_block(badge_font, icon_size, gap, line_y):
    """The badge as the core draws it: two lines right-aligned, the icon right of them"""
    icon = Image.open(os.path.join(root, "assets", "danik.png")).convert("RGBA").resize(icon_size, Image.LANCZOS)
    lines = ["Hybrid Core Framework v%d" % VERSION, "by danik"]
    tw = max(badge_font.getbbox(t)[2] for t in lines)
    block = Image.new("RGBA", (tw + gap + icon.width, icon.height), (0, 0, 0, 0))
    d = ImageDraw.Draw(block)
    for n, t in enumerate(lines):
        d.text((tw - badge_font.getbbox(t)[2], line_y + n * 15), t, font=badge_font, fill=INK + (255,))
    block.alpha_composite(icon, (tw + gap, 0))
    return block


def layer_badge(badge_font, t=1.0):
    """t 0: where and as large as the core's own screen has it; 1: the frontend's, in the footer"""
    wall = badge_block(badge_font, (53, 54), 12, 13)
    front = badge_block(badge_font, (36, 37), 8, 4)
    rx, by = lerp(W - WALL_EDGE, W - M, t), lerp(H - WALL_EDGE, FOOT[0] + 37, t)
    out = blank()
    for block, a in ((wall, 1 - t), (front, t)):
        if a > 0:
            out.alpha_composite(fade(block, a), (round(rx - block.width), round(by - block.height)))
    return out


def rows_for(ports, mode):
    games = [g for p in ports for g in p["games"]]
    if mode == "A-Z":
        return [("game", g["title"], g, "") for g in sorted(games, key=lambda g: g["title"].lower())]
    if mode == "By port":
        rows = []
        for p in sorted(ports, key=lambda p: p["engine"].lower()):
            rows.append(("head", p["engine"], None, ""))
            rows += [("game", g["title"], g, "") for g in p["games"]]
        return rows
    by_id = {g["id"]: g for g in games}
    return [("game", by_id[i]["title"], by_id[i], when) for i, when in RECENT]


MAIN_HINTS = [("ok", "Details"), ("fav", "Favourite"), ("ports", "Ports"), ("options", "Options"), ("back", "Exit")]


class Main:
    """The main screen's state and its layers"""

    def __init__(self, F, badge_font, ports, mode, cursor_id, family="ps"):
        self.F, self.badge_font, self.family, self.mode = F, badge_font, family, mode
        self.ports = ports
        self.rows = rows_for(ports, mode)
        self.count = "%d games" % sum(len(p["games"]) for p in ports)
        self.port_of = {g["id"] + g["engine"]: p for p in ports for g in p["games"]}
        self.strips = list_strips(F, self.rows)
        self.cursor = self.index(cursor_id)
        self.first = max(0, min(self.cursor - N_ROWS // 2, len(self.rows) - N_ROWS))

    def index(self, game_id):
        return next(n for n, r in enumerate(self.rows) if r[2] and r[2]["id"] == game_id)

    def game(self, n=None):
        return self.rows[self.cursor if n is None else n][2]

    def port(self, game):
        return self.port_of[game["id"] + game["engine"]]

    def frame_layers(self):
        F = self.F
        g = self.game()
        return {
            "ground": layer_ground(), "logo": layer_logo(), "title": layer_title(F), "count": layer_count(F, self.count),
            "chips": layer_chips(F, self.family, chip_rects(F)[self.mode]),
            "list_panel": layer_panel((LIST_X[0], MAIN[0], LIST_X[1], MAIN[1])),
            "prev_panel": layer_panel((PREV_X[0], MAIN[0], PREV_X[1], MAIN[1])),
            "rows": layer_rows(self.strips, len(self.rows), self.first, self.cursor),
            "prev_logo": layer_preview_logo(g), "prev_shot": layer_shot(F, g, preview_shot_xy()),
            "hints": layer_hints(F, MAIN_HINTS, self.badge_font, self.family), "badge": layer_badge(self.badge_font),
        }


ORDER = ["ground", "logo", "title", "count", "chips", "list_panel", "rows", "prev_panel", "prev_logo", "prev_shot",
         "hints", "badge"]


def compose(layers, order=ORDER):
    out = layers["ground"].copy() if "ground" in layers else blank()
    for name in order:
        if name != "ground" and name in layers:
            out.alpha_composite(layers[name])
    return out


def main_screen(F, badge_font, ports, mode, cursor_id, family="ps"):
    return compose(Main(F, badge_font, ports, mode, cursor_id, family).frame_layers()).convert("RGB")


# Details: A on a game. Everything the frontend knows of it; A plays, Y marks
# it a favourite, B goes back to the list, L and R go to the game before and
# after in the list.

def details_facts(game, port):
    year, dev, desc = INFO.get(game["id"], ("", "", ""))
    when = dict(RECENT).get(game["id"], "never")
    nodata = game["id"] in NO_DATA
    return [("Year", year, INK), ("Developer", dev, INK),
            ("Port", "%s %s" % (port["engine"], port["version"]), CYAN),
            ("Game data", "not found" if nodata else "games/dnk/%s/" % game["engine"], CORAL if nodata else INK),
            ("Menu entry", "_Other/dnk-%s" % game["title"], INK),
            ("Last played", when, INK)]


def layer_details_info(F, game, port, logo=True):
    """The top right panel's content: logo, title, the facts"""
    c = Canvas(ground=False)
    x0, y0, x1, y1 = D_INFO_PANEL
    pw = x1 - x0
    if logo:
        c.over(layer_details_logo(game))
    bx, by, bw, bh = details_logo_box()
    ty = by + bh + 8
    title = clip(game["title"], F.title, pw - 28 - (18 if game["id"] in FAVOURITES else 0))
    c.text((x0 + 14, ty), title, F.title, INK)
    if game["id"] in FAVOURITES:
        c.star(x0 + 24 + F.title.getlength(title), ty + 12, 7, GOLD)
    layer = c.layer()
    ImageDraw.Draw(layer).line((x0 + 14, ty + 31, x1 - 14, ty + 31), fill=EDGE + (90,))
    c.over(layer)
    lw = max(F.small.getlength(k) for k, v, ink in details_facts(game, port)) + 12
    for n, (k, v, ink) in enumerate(details_facts(game, port)):
        y = ty + 39 + n * 16
        c.text((x0 + 14, y + 1), k, F.small, DIM)
        c.text((x0 + 14 + lw, y), clip(v, F.chip, pw - 28 - lw), F.chip, ink)
    return c.im


def details_logo_box():
    x0, y0, x1, y1 = D_INFO_PANEL
    return x0 + 12, y0 + 10, x1 - x0 - 24, 46


def layer_details_logo(game):
    bx, by, bw, bh = details_logo_box()
    logo = logo_picture(game["logo"], bw, bh)
    out = blank()
    out.alpha_composite(logo, (bx + (bw - logo.width) // 2, by + bh - logo.height))
    return out


def layer_details_desc(F, game):
    c = Canvas(ground=False)
    x0, y0, x1, y1 = D_DESC_PANEL
    desc = INFO.get(game["id"], ("", "", ""))[2]
    lines = wrap(desc, F.desc, x1 - x0 - 32)
    top = y0 + (y1 - y0 - len(lines) * 17) // 2
    for n, line in enumerate(lines):
        c.text((x0 + 16, top + n * 17), line, F.desc, INK if n == 0 else INK)
    return c.im


def details_hints(game):
    if game["id"] in NO_DATA:
        return [("fav", "Favourite"), ("back", "Back")]
    return [("ok", "Play"), ("fav", "Favourite"), ("back", "Back")]


def details_layers(F, badge_font, game, port, family="ps"):
    return {"ground": layer_ground(), "logo": layer_logo(), "title": layer_title(F), "count": layer_count(F, "Details"),
            "shot_panel": layer_panel(D_SHOT_PANEL), "shot": layer_shot(F, game, D_SHOT),
            "info_panel": layer_panel(D_INFO_PANEL), "info": layer_details_info(F, game, port),
            "desc_panel": layer_panel(D_DESC_PANEL), "desc": layer_details_desc(F, game),
            "hints": layer_hints(F, details_hints(game), badge_font, family), "badge": layer_badge(badge_font)}


D_ORDER = ["ground", "logo", "title", "count", "shot_panel", "shot", "info_panel", "info", "desc_panel", "desc",
           "hints", "badge"]


def details_screen(F, badge_font, ports, game_id, family="ps"):
    m = Main(F, badge_font, ports, "A-Z", game_id, family)
    return compose(details_layers(F, badge_font, m.game(), m.port(m.game()), family), D_ORDER).convert("RGB")


def header(c, F, mode, count, family):
    c.over(layer_logo())
    c.over(layer_title(F))
    c.over(layer_count(F, count))
    if mode:
        c.over(layer_chips(F, family, chip_rects(F)[mode]))


def footer(c, F, hints, badge_font, family):
    c.over(layer_hints(F, hints, badge_font, family))
    c.over(layer_badge(badge_font))


def ports_screen(F, badge_font, ports, cursor_engine, family="ps"):
    c = Canvas()
    header(c, F, None, "12 ports", family)
    rows = [("set", "Launcher", "On"), ("set", "Menu entries", "Other, as dnk-<Title>")]
    for p in sorted(ports, key=lambda p: p["engine"].lower()):
        rows.append(("port", p["engine"], p["engine"] in INSTALLED, p))
        for g in p["games"]:
            rows.append(("game", g["title"], p["engine"] in INSTALLED, p))
    cursor = next(n for n, r in enumerate(rows) if r[0] == "port" and r[1] == cursor_engine)
    x0, x1 = M, 400
    y0, y1 = MAIN
    c.panel((x0, y0, x1, y1))
    n_rows = (y1 - y0 - 12) // ROW
    for n, row in enumerate(rows[:n_rows]):
        kind, label, val = row[:3]
        y = y0 + 6 + n * ROW
        mid = y + ROW / 2
        sel = n == cursor
        if sel:
            c.bar((x0 + 5, y + 1, x1 - 5, y + ROW - 1))
        ink = SEL_INK if sel else INK
        if kind == "set":
            c.text((x0 + 14, mid), label, F.row, ink, anchor="lm")
            c.pill((x1 - 14, mid - 9), val, F.small, CYAN, SEL_INK, anchor_right=True, h=18)
            continue
        bx = x0 + 14 + (22 if kind == "game" else 0)
        layer = c.layer()
        d = ImageDraw.Draw(layer)
        if val:
            d.rounded_rectangle((bx, mid - 7, bx + 14, mid + 7), 3, fill=(SEL_INK if sel else PINK) + (255,))
            d.line((bx + 3, mid, bx + 6, mid + 4, bx + 11, mid - 4), fill=(255, 255, 255, 255), width=2)
        else:
            d.rounded_rectangle((bx, mid - 7, bx + 14, mid + 7), 3, outline=(SEL_INK if sel else DIM) + (255,), width=2)
        c.over(layer)
        c.text((bx + 22, mid), label, F.row if kind == "game" else F.title, ink if kind == "game" or sel else CYAN, anchor="lm")
    port = next(p for p in ports if p["engine"] == cursor_engine)
    px0 = x1 + 12
    c.panel((px0, y0, W - M, y1))
    pw = W - M - px0
    logo = logo_picture(port["games"][0]["logo"], pw - 20, 80)
    c.picture(logo, (px0 + (pw - logo.width) // 2, y0 + 10 + 80 - logo.height))
    y = y0 + 104
    c.text((px0 + 14, y), port["engine"], F.title, INK)
    c.pill((px0 + 14, y + 28), port["version"], F.small, CYAN, SEL_INK, h=18)
    c.text((px0 + 14, y + 58), "%d games" % len(port["games"]), F.text, DIM)
    c.text((px0 + 14, y + 80), "Installed" if port["engine"] in INSTALLED else "Not installed", F.text,
           GOLD if port["engine"] in INSTALLED else DIM)
    footer(c, F, [("ok", "Tick"), ("fav", "Apply"), ("back", "Back")], badge_font, family)
    return c.im.convert("RGB")


OPTIONS = [  # the frontend's own settings, kept in games/dnk/dnkLauncher/settings.txt
    ("Font", "font", "The font of every screen here. Chakra Petch unless you pick another."),
    ("Colours", "colours", "The colours of every screen here. MiSTer Purple unless you pick another."),
    ("Flicker filter", "filter", "On a 15kHz screen 640x480 is interlaced: the filter softens what would "
                                 "blink. HDMI and 31kHz screens never need it."),
    ("Button symbols", "buttons", "Auto shows the buttons of the controller you use, when it is a Switch, "
                                  "Xbox or PlayStation one. Pick one for any other."),
    ("Sort games", "sort", "How the list is sorted when the frontend starts. Select changes it at any time."),
]
OPT_X = (M, 372)


def layer_options_left(F, fonts, cursor, values):
    c = Canvas(ground=False)
    x0, x1 = OPT_X
    y0, y1 = MAIN
    c.panel((x0, y0, x1, y1))
    for n, (label, key, help_text) in enumerate(OPTIONS):
        y = y0 + 10 + n * 34
        mid = y + 15
        sel = n == cursor
        if sel:
            c.bar((x0 + 5, y, x1 - 5, y + 30))
        c.text((x0 + 14, mid), label, F.row, SEL_INK if sel else INK, anchor="lm")
        value = values[key]
        vf = fonts[value][1] if key == "font" else F
        vtext = fonts[value][0] if key == "font" else THEMES[value]["label"] if key == "colours" else value
        vx = x1 - 30 - round(vf.row.getlength(vtext))
        c.text((vx, mid), vtext, vf.row, SEL_INK if sel else CYAN, anchor="lm")
        layer = c.layer()     # the arrows: left and right change it
        d = ImageDraw.Draw(layer)
        a = (SEL_INK if sel else DIM) + (255,)
        d.polygon((vx - 14, mid, vx - 8, mid - 5, vx - 8, mid + 5), fill=a)
        d.polygon((x1 - 16, mid, x1 - 22, mid - 5, x1 - 22, mid + 5), fill=a)
        c.over(layer)
    return c.im


def layer_options_right(F, fonts, cursor, values):
    c = Canvas(ground=False)
    label, key, help_text = OPTIONS[cursor]
    px0 = OPT_X[1] + 12
    y0, y1 = MAIN
    c.panel((px0, y0, W - M, y1))
    pw = W - M - px0
    c.text((px0 + 14, y0 + 12), label, F.title, INK)
    for n, line in enumerate(wrap(help_text, F.text, pw - 28)):
        c.text((px0 + 14, y0 + 44 + n * 19), line, F.text, DIM)
    y = y0 + 44 + len(wrap(help_text, F.text, pw - 28)) * 19 + 12
    if key == "font":   # every choice in itself
        for name, (flabel, f) in fonts.items():
            c.text((px0 + 14, y), flabel, f.row, PINK if name == values["font"] else INK)
            y += 26
    if key == "colours":    # every scheme: its ground, a panel and its bar
        for name, t in THEMES.items():
            sw = Image.new("RGBA", (44, 18))
            d = ImageDraw.Draw(sw)
            top, bottom = rgb(t["bg"][0]), rgb(t["bg"][1])
            for yy in range(18):
                d.line((0, yy, 44, yy), fill=tuple(round(a + (b - a) * yy / 17) for a, b in zip(top, bottom)) + (255,))
            a, b = rgb(t["bar"][0]), rgb(t["bar"][1])
            for xx in range(8, 40):
                d.line((xx, 6, xx, 12), fill=tuple(round(p + (q - p) * (xx - 8) / 31) for p, q in zip(a, b)) + (255,))
            mask = Image.new("L", (44, 18), 0)
            ImageDraw.Draw(mask).rounded_rectangle((0, 0, 43, 17), 5, fill=255)
            sw.putalpha(mask)
            c.picture(sw, (px0 + 14, y))
            ImageDraw.Draw(c.im).rounded_rectangle((px0 + 14, y, px0 + 57, y + 17), 5, outline=EDGE + (160,))
            c.text((px0 + 66, y + 9), t["label"], F.row, PINK if name == values["colours"] else INK, anchor="lm")
            y += 22
    return c.im


OPTION_HINTS = [("ok", "Change"), ("back", "Back")]


def options_screen(F, badge_font, fonts, cursor, values, family="ps"):
    """fonts: the Fonts of every choice, to show each in itself"""
    layers = {"ground": layer_ground(), "logo": layer_logo(), "title": layer_title(F), "count": layer_count(F, "Options"),
              "left": layer_options_left(F, fonts, cursor, values), "right": layer_options_right(F, fonts, cursor, values),
              "hints": layer_hints(F, OPTION_HINTS, badge_font, family), "badge": layer_badge(badge_font)}
    return compose(layers, ["logo", "title", "count", "left", "right", "hints", "badge"]).convert("RGB")


# The animations, a frame per field (59.64 a second). Whatever the player
# presses while one runs takes it to its end at once: input is never kept
# waiting for an animation.

def anim_start(F, badge_font, ports):
    """From the core's own screen (its ground, the logo in the middle, the badge)
    to the list: the logo goes to its corner, the badge to the footer, the rest
    comes up after them"""
    m = Main(F, badge_font, ports, "A-Z", "doom2")
    L = m.frame_layers()
    flat = Image.new("RGBA", (W, H), WALL_BG + (255,))
    groups = [  # layer(s), from field, to field, coming from dx, dy
        (["title"], 18, 32, 16, 0), (["count"], 22, 34, 16, 0), (["chips"], 22, 36, 0, -8),
        (["list_panel", "rows"], 20, 36, 0, 18), (["prev_panel", "prev_logo", "prev_shot"], 24, 40, 0, 18),
        (["hints"], 28, 44, 0, 10)]
    frames = []
    for f in range(46):
        fr = flat.copy()
        fr.alpha_composite(place(L["ground"], alpha=ease_in_out(span(f, 4, 28))))
        t = ease_in_out(span(f, 4, 30))
        fr.alpha_composite(layer_logo(lerp(1.0, 0.5, t), (lerp(WALL_LOGO[0], M - 4, t), lerp(WALL_LOGO[1], HEAD[0], t))))
        fr.alpha_composite(layer_badge(badge_font, t))
        for names, a, b, dx, dy in groups:
            p = ease_out(span(f, a, b))
            if p > 0:
                for n in names:
                    fr.alpha_composite(place(L[n], dx * (1 - p), dy * (1 - p), p))
        frames.append(fr.convert("RGB"))
    return frames


def anim_cursor(F, badge_font, ports):
    """Down six games: the bar glides, the list scrolls when the bar is near
    its edge, the preview crosses over to the next game"""
    m = Main(F, badge_font, ports, "A-Z", "doom2")
    path = [m.game(m.cursor + n)["id"] for n in range(7)]
    L = m.frame_layers()
    still = [k for k in ORDER if k not in ("rows", "prev_logo", "prev_shot")]
    fixed = compose({k: L[k] for k in still}, still)
    stops, first = [], m.first
    for gid in path:
        n = m.index(gid)
        if n - first > N_ROWS - 3:
            first = min(n - (N_ROWS - 3), len(m.rows) - N_ROWS)
        stops.append((n, first, layer_preview(F, m.game(n)), m.game(n)))
    frames = []
    for i in range(len(stops) - 1):
        (c0, f0, p0, g0), (c1, f1, p1, g1) = stops[i], stops[i + 1]
        for f in range(14):
            e = ease_out(span(f, 0, 7))
            fr = fixed.copy()
            fr.alpha_composite(layer_rows(m.strips, len(m.rows), lerp(f0, f1, e), lerp(c0, c1, e)))
            fr.alpha_composite(place(p0, alpha=1 - span(f, 0, 5)))
            pin = ease_out(span(f, 3, 10))
            if pin > 0:
                fr.alpha_composite(place(layer_preview(F, g1, logo_dx=10 * (1 - pin)), alpha=pin))
            frames.append(fr.convert("RGB"))
    return frames


def anim_sort(F, badge_font, ports):
    """Select: A-Z to By port. The pill slides, the old list leaves to the left,
    the new one comes from the right; the cursor stays on the same game"""
    a = Main(F, badge_font, ports, "A-Z", "doom2")
    b = Main(F, badge_font, ports, "By port", "doom2")
    L = a.frame_layers()
    still = [k for k in ORDER if k not in ("rows", "chips")]
    fixed = compose({k: L[k] for k in still}, still)
    ra, rb = chip_rects(F)["A-Z"], chip_rects(F)["By port"]
    frames = []
    for f in range(16):
        e = ease_out(span(f, 0, 9))
        fr = fixed.copy()
        fr.alpha_composite(layer_chips(F, a.family, (lerp(ra[0], rb[0], e), lerp(ra[1], rb[1], e))))
        out = ease_out(span(f, 0, 5))
        if out < 1:
            fr.alpha_composite(place(layer_rows(a.strips, len(a.rows), a.first, a.cursor, dx=-24 * out), alpha=1 - out))
        e2 = ease_out(span(f, 4, 14))
        if e2 > 0:
            fr.alpha_composite(place(layer_rows(b.strips, len(b.rows), b.first, b.cursor, dx=24 * (1 - e2)), alpha=e2))
        frames.append(fr.convert("RGB"))
    return frames


def anim_details(F, badge_font, ports, back=False):
    """A on a game: its screenshot moves from the preview to its place in
    Details and the logo to the facts' panel, the list leaves to the left,
    Details' panels come up around them. B is the same backwards"""
    m = Main(F, badge_font, ports, "A-Z", "doom2")
    g = m.game()
    L = m.frame_layers()
    D = details_layers(F, badge_font, g, m.port(g), m.family)
    info_text = layer_details_info(F, g, m.port(g), logo=False)
    base = compose({k: L[k] for k in ("ground", "logo", "title", "badge")}, ["logo", "title", "badge"])
    # the logo: from its box in the preview to Details', scaled
    pw = PREV_X[1] - PREV_X[0]
    big = logo_picture(g["logo"], pw - 24, LOGO_H)
    sy = preview_shot_xy()[1]
    l0 = (PREV_X[0] + (pw - big.width) // 2, sy - 12 - big.height, big.width, big.height)
    bx, by, bw, bh = details_logo_box()
    small = logo_picture(g["logo"], bw, bh)
    l1 = (bx + (bw - small.width) // 2, by + bh - small.height, small.width, small.height)
    src_logo = Image.open(g["logo"]).convert("RGBA")
    src_logo = src_logo.crop(src_logo.getbbox())
    frames = []
    n = 22
    for f in range(n):
        k = n - 1 - f if back else f
        fr = base.copy()
        fr.alpha_composite(place(L["count"], alpha=1 - span(k, 0, 6)))
        fr.alpha_composite(place(D["count"], alpha=span(k, 6, 12)))
        fr.alpha_composite(place(L["chips"], 0, -6 * ease_in_out(span(k, 0, 7)), 1 - span(k, 0, 7)))
        p = ease_in_out(span(k, 0, 9))
        for name in ("list_panel", "rows"):
            fr.alpha_composite(place(L[name], -28 * p, 0, 1 - p))
        fr.alpha_composite(place(L["prev_panel"], alpha=1 - span(k, 0, 7)))
        fr.alpha_composite(place(L["hints"], alpha=1 - span(k, 0, 6)))
        fr.alpha_composite(place(D["hints"], alpha=span(k, 9, 16)))
        q = ease_out(span(k, 4, 14))
        fr.alpha_composite(place(D["shot_panel"], alpha=q))
        r = ease_out(span(k, 9, 18))
        fr.alpha_composite(place(D["info_panel"], 0, 14 * (1 - r), r))
        fr.alpha_composite(place(info_text, 0, 14 * (1 - r), r))
        s = ease_out(span(k, 11, 21))
        for name in ("desc_panel", "desc"):
            fr.alpha_composite(place(D[name], 0, 14 * (1 - s), s))
        t = ease_in_out(span(k, 2, 15))   # the screenshot and the logo travel
        x0, y0 = preview_shot_xy()
        fr.alpha_composite(layer_shot(F, g, (lerp(x0, D_SHOT[0], t), lerp(y0, D_SHOT[1], t))))
        box = [lerp(a, b, t) for a, b in zip(l0, l1)]
        logo = src_logo.resize((max(1, round(box[2])), max(1, round(box[3]))), Image.LANCZOS)
        fr.alpha_composite(logo, (round(box[0]), round(box[1])))
        frames.append(fr.convert("RGB"))
    return frames


def anim_options(F, badge_font, ports, fonts, values):
    """Start: the main screen's panels sink and go, Options' rise"""
    m = Main(F, badge_font, ports, "A-Z", "doom2")
    L = m.frame_layers()
    base = compose({k: L[k] for k in ("ground", "logo", "title", "badge")}, ["logo", "title", "badge"])
    left, right = layer_options_left(F, fonts, 0, values), layer_options_right(F, fonts, 0, values)
    opt_count = layer_count(F, "Options")
    opt_hints = layer_hints(F, OPTION_HINTS, badge_font, m.family)
    leaving = [(["list_panel", "rows"], 0, 7), (["prev_panel", "prev_logo", "prev_shot"], 1, 8)]
    frames = []
    for f in range(20):
        fr = base.copy()
        fr.alpha_composite(place(L["chips"], alpha=1 - span(f, 0, 6)))
        fr.alpha_composite(place(L["count"], alpha=1 - span(f, 0, 6)))
        fr.alpha_composite(place(opt_count, alpha=span(f, 5, 12)))
        fr.alpha_composite(place(L["hints"], alpha=1 - span(f, 0, 6)))
        fr.alpha_composite(place(opt_hints, alpha=span(f, 6, 13)))
        for names, a, b in leaving:
            p = ease_in_out(span(f, a, b))
            if p < 1:
                for n in names:
                    fr.alpha_composite(place(L[n], 0, 12 * p, 1 - p))
        for layer, a, b in ((left, 6, 15), (right, 8, 17)):
            p = ease_out(span(f, a, b))
            if p > 0:
                fr.alpha_composite(place(layer, 0, 14 * (1 - p), p))
        frames.append(fr.convert("RGB"))
    return frames


def anim_play(F, badge_font, ports):
    """A in Details: the screenshot's frame flashes, the screenshot grows to
    the whole screen while the rest goes dark, then black; then the game's
    core is loaded"""
    m = Main(F, badge_font, ports, "A-Z", "doom2")
    g = m.game()
    full = compose(details_layers(F, badge_font, g, m.port(g), m.family), D_ORDER)
    src = Image.open(g["shot"]).convert("RGB")
    sx, sy = D_SHOT
    frames = []
    for f in range(38):
        fr = full.copy()
        flash = 1 - span(f, 0, 7)
        if flash > 0:
            glow = blank()
            ImageDraw.Draw(glow).rounded_rectangle((sx - 6, sy - 6, sx + SHOT[0] + 5, sy + SHOT[1] + 5), 6,
                                                   outline=PINK + (round(255 * flash),), width=4)
            fr.alpha_composite(glow.filter(ImageFilter.GaussianBlur(2)))
        dark = ease_in_out(span(f, 3, 16))
        if dark > 0:
            fr.alpha_composite(Image.new("RGBA", (W, H), (0, 0, 0, round(255 * dark))))
        t = ease_in_out(span(f, 3, 22))
        if f >= 3:
            w, h = lerp(SHOT[0], W, t), lerp(SHOT[1], H, t)
            fr.paste(src.resize((round(w), round(h)), Image.BILINEAR), (round(lerp(sx, 0, t)), round(lerp(sy, 0, t))))
        out = span(f, 24, 36)
        if out > 0:
            fr.alpha_composite(Image.new("RGBA", (W, H), (0, 0, 0, round(255 * out))))
        frames.append(fr.convert("RGB"))
    return frames


def save_animation(frames, path, slow=1):
    """An animated WebP at the field rate (slow: that many times slower), with
    a pause before and after"""
    durations = [round((i + 1) * FIELD_MS * slow) - round(i * FIELD_MS * slow) for i in range(len(frames))]
    durations[0] += 500
    durations[-1] += 1200
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=durations, loop=0,
                   lossless=False, quality=80, method=6)


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: dnklauncher_mockup.py <fonts dir> <out dir> [repositories...]")
    fonts_dir, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    repos = sys.argv[3:] or sorted(glob.glob(os.path.expanduser("~/MiSTer-*")))
    ports = load_ports(repos)
    badge = cozette(out)
    fonts = {key: (FONTS[key][0].replace(" SemiBold", "").replace(" ExtraBold", "").replace(" Bold", "")
                   .replace(" Medium", ""), Fonts(fonts_dir, key)) for key in FONTS}
    screens = {}
    for key, (label, F) in fonts.items():
        screens[key + "_az"] = main_screen(F, badge, ports, "A-Z", "doom2")
        screens[key + "_details"] = details_screen(F, badge, ports, "doom2")
    F = fonts["chakra"][1]
    for family in FAMILIES:
        screens["buttons_" + family] = main_screen(F, badge, ports, "A-Z", "doom2", family)
    screens["chakra_byport"] = main_screen(F, badge, ports, "By port", "Q2")
    screens["chakra_recent"] = main_screen(F, badge, ports, "Recent", "DN3D")
    screens["chakra_nodata"] = main_screen(F, badge, ports, "A-Z", "hexen")
    screens["details_dn3d"] = details_screen(F, badge, ports, "DN3D")
    screens["details_nodata"] = details_screen(F, badge, ports, "hexen")
    screens["chakra_ports"] = ports_screen(F, badge, ports, "CrispyDoom")
    values = {"font": "chakra", "colours": "purple", "filter": "On", "buttons": "Auto: PlayStation", "sort": "A-Z"}
    screens["options_font"] = options_screen(F, badge, fonts, 0, values)
    screens["options_colours"] = options_screen(F, badge, fonts, 1, values)
    screens["options_buttons"] = options_screen(F, badge, fonts, 3, values)
    for theme in THEMES:     # the colour schemes
        apply_theme(theme)
        screens["theme_%s_main" % theme] = main_screen(F, badge, ports, "A-Z", "doom2")
        screens["theme_%s_details" % theme] = details_screen(F, badge, ports, "doom2")
        screens["theme_%s_options" % theme] = options_screen(F, badge, fonts, 1, dict(values, colours=theme))
    apply_theme("purple")
    for name, im in screens.items():
        path = os.path.join(out, name + ".png")
        im.save(path)
        print(path)
    animations = {"start": anim_start(F, badge, ports), "cursor": anim_cursor(F, badge, ports),
                  "sort": anim_sort(F, badge, ports), "details": anim_details(F, badge, ports),
                  "back": anim_details(F, badge, ports, back=True),
                  "options": anim_options(F, badge, ports, fonts, values), "play": anim_play(F, badge, ports)}
    for name, frames in animations.items():
        for slow, suffix in ((1, ""), (4, "_slow")):
            path = os.path.join(out, "anim_%s%s.webp" % (name, suffix))
            save_animation(frames, path, slow)
            print(path, len(frames), "frames")


if __name__ == "__main__":
    main()
