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
# usage: dnklauncher_mockup.py --layers <fonts dir> <out dir> [repositories...]
#   every theme's main screen, Details and Options without their ground, and
#   the themes' colours (themes.json), for the page that tries grounds (GROUND)
import glob
import math
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
VERSION = 18                    # of hybrid_host.sv, in the badge


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

# Screen margin in Options (Danik, 2026-11-01): what is on the screen moves that
# many pixels in from all its edges. 0 to 10 in twos; DNK_MOCK_MARGIN for a mock-up.
MARGIN = int(os.environ.get("DNK_MOCK_MARGIN", 0))
M = 30 + MARGIN                 # from the sides; a 15kHz screen hides some of them

# The player's choice in Options (Danik, 2026-10-25: Chakra Petch the default;
# their places in tools/dnklauncher_fonts.py's ranking by flicker after them)
# big and mid: a title where a logo is (a ScummVM game's, title_picture())
SIZES = dict(row=16, text=15, title=20, small=12, chip=13, head=18, desc=14, big=26, mid=22)
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
        "select": ("-", None, None), "start": ("+", None, None),
        "l1": ("L", None, None), "r1": ("R", None, None), "l2": ("ZL", None, None), "r2": ("ZR", None, None)}),
    "xbox": ("Xbox", {
        "south": ("A", (0x6C, 0xC6, 0x2E), (0x22, 0x22, 0x26)), "east": ("B", (0xEE, 0x4B, 0x4B), (0x22, 0x22, 0x26)),
        "north": ("Y", (0xF5, 0xC4, 0x1A), (0x22, 0x22, 0x26)), "west": ("X", (0x3F, 0x8F, 0xEF), (0x22, 0x22, 0x26)),
        "select": ("VIEW", None, None), "start": ("MENU", None, None),
        "l1": ("LB", None, None), "r1": ("RB", None, None), "l2": ("LT", None, None), "r2": ("RT", None, None)}),
    "ps": ("PlayStation", {
        "south": ("cross", (0x8F, 0xB4, 0xF2), (0x22, 0x22, 0x2A)), "east": ("circle", (0xF2, 0x6B, 0x75), (0x22, 0x22, 0x2A)),
        "north": ("triangle", (0x4F, 0xD1, 0xB0), (0x22, 0x22, 0x2A)), "west": ("square", (0xEE, 0x9C, 0xDC), (0x22, 0x22, 0x2A)),
        "select": ("CREATE", None, None), "start": ("OPTIONS", None, None),
        "l1": ("L1", None, None), "r1": ("R1", None, None), "l2": ("L2", None, None), "r2": ("R2", None, None)}),
    "snes": ("SNES (MiSTer's names)", {
        "south": ("B", (0xFF, 0xFF, 0xFF), (0xF5, 0xC5, 0x42)), "east": ("A", (0xFF, 0xFF, 0xFF), (0xE8, 0x4A, 0x5F)),
        "north": ("X", (0xFF, 0xFF, 0xFF), (0x4A, 0x90, 0xE2)), "west": ("Y", (0xFF, 0xFF, 0xFF), (0x5C, 0xC2, 0x6A)),
        "select": ("SELECT", None, None), "start": ("START", None, None),
        "l1": ("L", None, None), "r1": ("R", None, None), "l2": ("L2", None, None), "r2": ("R2", None, None)}),
}
# Where the frontend's actions are: by letter (Danik, 2026-10-25), the same
# letter on every pad: Play A, Exit B, Favourite Y, Repo X (PlayStation:
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


# The legend's second row (Danik, 2026-10-31): the bumpers and triggers, which
# are not each a button's own action as the first row's are, and are not clicked
SHOULDERS = {"page": ("l1", "r1"), "layout": ("l2", "r2"), "l2": ("l2",)}


class Fonts:
    def __init__(self, fonts_dir, key):
        label, path, weight, sizes = FONTS[key]
        self.label = label
        self.file, self.weight = os.path.join(fonts_dir, path), weight
        for role, size in sizes.items():
            f = ImageFont.truetype(os.path.join(fonts_dir, path), size, layout_engine=ImageFont.Layout.RAQM)
            if weight:
                f.set_variation_by_axes([weight])
            setattr(self, role, f)

    def sized(self, size):
        """The font at a size of no role (the title in a logo's place, Systems)"""
        f = ImageFont.truetype(self.file, size, layout_engine=ImageFont.Layout.RAQM)
        if self.weight:
            f.set_variation_by_axes([self.weight])
        return f


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

    def panel(self, box, radius=10, alpha=None):
        """alpha: the body's, if not the scheme's"""
        x0, y0, x1, y1 = box
        shadow = self.layer()
        ImageDraw.Draw(shadow).rounded_rectangle((x0 + 2, y0 + 5, x1 + 2, y1 + 5), radius, fill=(20, 0, 30, 120))
        self.over(shadow.filter(ImageFilter.GaussianBlur(5)))
        body = self.layer()
        d = ImageDraw.Draw(body)
        d.rounded_rectangle(box, radius, fill=PANEL + (alpha or PANEL_ALPHA,), outline=EDGE + (150,), width=1)
        d.line((x0 + radius, y0 + 1, x1 - radius, y0 + 1), fill=(255, 255, 255, 50))   # light from above
        self.over(body)

    def text(self, xy, s, font, colour, anchor="la", alpha=255):
        layer = self.layer()
        ImageDraw.Draw(layer).text(xy, s, font=font, fill=colour + (alpha,), anchor=anchor)
        self.over(layer)

    def bar(self, box, radius=0, panel=None):
        """The cursor's bar: PINK to PEACH, square (Danik, 2026-10-27), from
        the panel's rim to the scroll bar's place (SB_W), which it never goes
        under; within the panel (its box) round at its corners as the rim is"""
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
        self.over(clip_to_panel(layer, panel) if panel else layer)

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
# Hidden with Shift + Favourite (Danik, 2026-11-01); with Options' Show hidden on
# they are in the lists, dimmed and marked, so that they can be unhidden
HIDDEN = set()
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

HEAD = (19 + MARGIN, 75 + MARGIN)     # the MiSTer logo and the sort modes
# the pages' panels (Details, Options, the ports page): the gaps above and below go first
MAIN = (83 + MARGIN - min(MARGIN, 4), 393 - MARGIN + min(MARGIN, 4))
FOOT = (405 - MARGIN, 441 - MARGIN)   # the buttons and the badge
HINT_ROW = 19             # the legend's second row under its first
LIST_X = (M, 330 - MARGIN)
PREV_X = (342 - MARGIN, W - M)
ROW = 24
SB_W = 4                  # the scroll bar's place at a list panel's right, within its rim: the bar ends before it
SHOT = (256, 192)         # the screenshot, in the preview and in Details
LOGO_H = 64               # the game's logo in the preview
# The list and the preview, as high as the preview needs (Danik, 2026-10-27):
# the logo PREV_TOP from its top, the screenshot's frame 4 pixels from its
# bottom rim as from its sides; centred between HEAD and FOOT
PREV_TOP = 21
LIST_H = PREV_TOP + LOGO_H + 12 + SHOT[1] + 6
LIST_Y = (HEAD[1] + (FOOT[0] - HEAD[1] - LIST_H) // 2, HEAD[1] + (FOOT[0] - HEAD[1] - LIST_H) // 2 + LIST_H)
N_ROWS = (LIST_H - 6) // ROW   # 12, the first and the last 3 or 4 pixels from the rim
LIST_PAD = (LIST_H - N_ROWS * ROW) // 2
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


def clip_to_panel(layer, box, radius=10):
    """What is drawn in a panel kept to its inside, within its rim: round at
    the corners as the rim is (the bar and the scroll bar; Danik, 2026-10-27)"""
    x0, y0, x1, y1 = box
    mask = Image.new("L", layer.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((x0 + 1, y0 + 1, x1 - 1, y1 - 1), radius - 1, fill=255)
    out = layer.copy()
    out.putalpha(ImageChops.multiply(out.getchannel("A"), mask))
    return out


def scroll_bar(box, n, n_total, first):
    """The scroll bar of a list of n_total in a panel (its box) showing n from
    row `first` (a fraction while it moves): at its right, within its rim from
    top to bottom, round at the corners as the panel is (Danik, 2026-10-27);
    the knob's ends round, their outer pixels left out. A layer, laid over the
    cursor's bar"""
    out = blank()
    if n_total <= n:
        return out
    x0, y0, x1, y1 = box
    sh = y1 - y0 - 1
    knob = max(16, sh * n // n_total)
    k0 = y0 + 1 + round((sh - knob) * first / (n_total - n))
    k1 = k0 + knob - 1
    ImageDraw.Draw(out).rectangle((x1 - SB_W, y0 + 1, x1 - 1, y1 - 1), fill=EDGE + (60,))
    k = blank()
    d = ImageDraw.Draw(k)
    d.rectangle((x1 - SB_W, k0 + 1, x1 - 1, k1 - 1), fill=EDGE + (230,))
    d.rectangle((x1 - 3, k0, x1 - 2, k0), fill=EDGE + (230,))
    d.rectangle((x1 - 3, k1, x1 - 2, k1), fill=EDGE + (230,))
    out.alpha_composite(k)
    return clip_to_panel(out, box)


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


# The ground: the theme's gradient, or a pattern over it that may scroll
# (Danik, 2026-10-26: a checker of 32, 25% darker, moving down and right a
# pixel a frame of an interlaced screen, the default; the pattern, its
# intensity, its strength across the screen, its direction and its speed are
# options; GROUND["pattern"] None is the gradient alone, the option Off). Every pixel of a pattern is one of two grounds, A or B, never a mix:
# the program puts the screen together over each once and copies every row
# from the two in runs, so a moving pattern costs a copy of the frame, not the
# whole screen laid together again. So no soft edges, and the pattern moves by
# whole pixels. A is the gradient (or the theme's middle colour, flat), B is A
# shaded: lighter or darker by `shade` (-1..1), as much everywhere or more at
# the top, the bottom or the edges (`fade`), as B may be any picture; or
# ("custom") A and B two colours of their own, "a" and "b".
# `line` is the share of a cell the grid's lines, the dots and the mortar take.
GROUND = dict(pattern="checker", size=32, line=0.125, shade=-0.25, fade="even", ground="gradient", a="7F30A0",
              b="5A1F75", direction=(1, 1), step=1.0)
PATTERNS = ("checker", "diamonds", "stripes", "columns", "rows", "grid", "dots", "zigzag", "bricks")


def pattern_mask(pattern, size, line=0.125, ox=0, oy=0, w=W, h=H):
    """Where B is: a W x H array of bools, the pattern moved by (ox, oy)"""
    import numpy as np
    s = max(2, int(size))
    v, u = np.mgrid[0:h, 0:w]
    u, v = u - int(ox), v - int(oy)   # moved right and down by (ox, oy)
    lw = max(1, round(s * line))
    if pattern == "checker":
        return ((u // s + v // s) & 1) == 1
    if pattern == "diamonds":
        return (((u + v) // s + (u - v) // s) & 1) == 1
    if pattern == "stripes":
        return (((u + v) // s) & 1) == 1
    if pattern == "columns":
        return ((u // s) & 1) == 1
    if pattern == "rows":
        return ((v // s) & 1) == 1
    if pattern == "grid":
        return (u % s < lw) | (v % s < lw)
    if pattern == "dots":
        dx, dy = u % s - (s - 1) / 2, v % s - (s - 1) / 2
        r = s * (0.18 + line)
        return dx * dx + dy * dy <= r * r
    if pattern == "zigzag":
        return (((v + np.abs(u % s - s // 2)) // max(1, s // 2)) & 1) == 1
    if pattern == "bricks":
        bh = max(2, s // 2)
        row = v // bh
        return (v % bh < lw) | ((u + (row & 1) * (s // 2)) % s < lw)
    raise ValueError(pattern)


def ground_pair(g=None):
    """The two grounds of a pattern, A and B, as float arrays H x W x 3"""
    import numpy as np
    g = dict(GROUND, **(g or {}))
    t = np.linspace(0, 1, H)[:, None, None]
    top, bottom = np.array(BG_TOP, float), np.array(BG_BOTTOM, float)
    if g["ground"] == "custom":   # two colours of the player's, A flat
        a = np.broadcast_to(np.array(rgb(g["a"]), float), (H, W, 3)).copy()
    elif g["ground"] == "flat":
        a = np.broadcast_to((top + bottom) / 2, (H, W, 3)).copy()
    else:
        a = np.broadcast_to(top + (bottom - top) * t, (H, W, 3)).copy()
    yy, xx = np.mgrid[0:H, 0:W]
    k = {"even": np.ones((H, W)), "top": 1 - 0.85 * yy / (H - 1), "bottom": 0.15 + 0.85 * yy / (H - 1),
         "edges": np.clip(np.hypot((xx - W / 2) / (W / 2), (yy - H / 2) / (H / 2)) * 1.1 - 0.15, 0, 1)}[g["fade"]]
    if g["ground"] == "custom":
        return a, a + (np.array(rgb(g["b"]), float) - a) * k[:, :, None]
    sh = g["shade"] * k[:, :, None]
    b = np.where(sh < 0, a * (1 + sh), a + (255 - a) * sh)
    return a, b


def layer_ground(phase=0, g=None):
    """The ground at the pattern's phase: the frame `phase`, moving by
    `step` pixels a frame along `direction`"""
    g = dict(GROUND, **(g or {}))
    if not g["pattern"]:
        return Canvas().im
    import numpy as np
    a, b = ground_pair(g)
    d = int(phase * g["step"])
    mask = pattern_mask(g["pattern"], g["size"], g["line"], d * g["direction"][0], d * g["direction"][1])
    out = np.where(mask[:, :, None], b, a)
    return Image.fromarray(np.dstack([np.round(out), np.full((H, W), 255.0)]).astype("uint8"), "RGBA")


def layer_logo(scale=0.5, xy=(M - 4, HEAD[0])):
    out = blank()
    out.paste(mister_logo(scale), (round(xy[0]), round(xy[1])))
    return out


def layer_title(F, title="Hybrid Ports"):
    """The System's name (Systems: the screen's)"""
    c = Canvas(ground=False)
    c.text((W - M, HEAD[0] + 2), title, F.title, INK, anchor="ra")
    return c.im


def layer_count(F, count, title="Hybrid Ports"):
    c = Canvas(ground=False)
    c.text((W - M - F.title.getlength(title) - 10, HEAD[0] + 7), count, F.small, DIM, anchor="ra")
    return c.im


CHIP_STAR = "*"      # the filter of favourites: a star for its name
CHIP_STAR_W = 30


def chip_name(d, F, m, x, w, y0, ink):
    """A chip's name, or its star"""
    if m == CHIP_STAR:
        cx, cy = x + w / 2, y0 + 10
        d.polygon([(cx + (7 if i % 2 == 0 else 7 * 0.45) * math.cos(-math.pi / 2 + i * math.pi / 5),
                    cy + (7 if i % 2 == 0 else 7 * 0.45) * math.sin(-math.pi / 2 + i * math.pi / 5)) for i in range(10)],
                  fill=ink + (255,))
    else:
        d.text((x + w / 2, y0 + 10), m, font=F.chip, fill=ink + (255,), anchor="mm")


def chip_rects(F, chips=None):
    rects, x = {}, W - M
    for m in reversed(chips or CHIPS):
        w = CHIP_STAR_W if m == CHIP_STAR else round(F.chip.getlength(m)) + 16
        x -= w
        rects[m] = (x, w)
        x -= 6
    return rects


def layer_chips(F, family, on, chips=None):
    """The sort modes, the pink pill at on = (x, width): a chip's, or between two while it slides"""
    rects = chip_rects(F, chips)
    c = Canvas(ground=False)
    sel = FAMILIES[family][1]["select"][0]
    sw = 18 if sel in "+-" else round(F.small.getlength(sel)) + 8
    left = min(x for x, w in rects.values())
    if chips is not BAN_CHIPS:   # Select's symbol before them; Systems' filters without it (Danik, 2026-11-01)
        button(c, F, family, places(family)["sort"], left - 6 - sw, HEAD[0] + 33)
    y0, y1 = HEAD[0] + 32, HEAD[0] + 52
    layer = c.layer()
    d = ImageDraw.Draw(layer)
    for m, (x, w) in rects.items():
        d.rounded_rectangle((x, y0, x + w, y1), 10, fill=PANEL + (150,), outline=EDGE + (120,))
        chip_name(d, F, m, x, w, y0, DIM)
    c.over(layer)
    ox, ow = on
    pill = blank()
    ImageDraw.Draw(pill).rounded_rectangle((ox, y0, ox + ow, y1), 10, fill=PINK + (255,))
    c.over(pill)
    ink = blank()
    d = ImageDraw.Draw(ink)
    for m, (x, w) in rects.items():
        chip_name(d, F, m, x, w, y0, SEL_INK)
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
            if sel:   # the bar's colours without a break: layer_rows() cuts the bar out where it is
                c.bar((1, y, w - SB_W, y + ROW))
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
            hidden = game["id"] in HIDDEN
            if hidden:
                c.text((right, mid), "hidden", F.small, SEL_INK if sel else DIM, anchor="rm")
                right -= F.small.getlength("hidden") + 8
            ink = SEL_INK if sel else (DIM if nodata or hidden else INK)
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
    ImageDraw.Draw(mask).rectangle((1, by + 1, w - SB_W - 1, by + ROW - 2), fill=255)
    view.paste(selected.crop((0, top, w, top + h)), (0, 0), mask)
    out = blank()
    out.paste(view, (x0 + round(dx), LIST_Y[0] + LIST_PAD))
    out = clip_to(out, (x0 + 1, LIST_Y[0] + 2, x1 - 1, LIST_Y[1] - 2))
    out = clip_to_panel(out, (x0, LIST_Y[0], x1, LIST_Y[1]))
    out.alpha_composite(scroll_bar((x0, LIST_Y[0], x1, LIST_Y[1]), N_ROWS, n_total, scroll))
    return out


def preview_shot_xy():
    pw = PREV_X[1] - PREV_X[0]
    top = LIST_Y[0] + PREV_TOP
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


# The game's cover (Danik, 2026-11-01; docs/dnklauncher.md, "Covers"): exactly 2:3,
# his pictures being 600x900, in the preview's panel in place of its logo and
# screenshot, centred, in the same dark frame
COVER = (178, 267)


def cover_path(game):
    return os.path.join(os.path.dirname(os.path.dirname(game["logo"])), "covers", game["id"] + ".png")


def preview_cover_xy():
    pw = PREV_X[1] - PREV_X[0]
    return PREV_X[0] + (pw - COVER[0]) // 2, LIST_Y[0] + (LIST_Y[1] - LIST_Y[0] - COVER[1]) // 2


def layer_cover(game):
    """The cover in its dark frame"""
    c = Canvas(ground=False)
    x, y = preview_cover_xy()
    layer = c.layer()
    ImageDraw.Draw(layer).rectangle((x - 2, y - 2, x + COVER[0] + 1, y + COVER[1] + 1), fill=(15, 0, 22, 255))
    c.over(layer)
    key = ("cover", game["logo"])
    if key not in _cache:
        _cache[key] = to_linear_resize(Image.open(cover_path(game)).convert("RGB"), COVER)
    c.picture(_cache[key], (x, y))
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


def hint_button(c, F, family, action, x, y):
    """The action's button, or the two of a pair, at x; returns the width"""
    if action in SHOULDERS:
        w = 0
        for place_ in SHOULDERS[action]:
            w += button(c, F, family, place_, x + w, y) + 2
        return w - 2
    return button(c, F, family, places(family)[action], x, y)


def layer_hints(F, hints, badge_font, family, boxes=None, second=None):
    """hints: (action, what it does); the action's button as the family prints
    it. boxes: a list that gets each hint's box (x0, y0, x1, y1), where a click
    does what it says. second: the second row's, under them; of those only
    Options has a box (Danik, 2026-11-01: it went there from the first row)"""
    c = Canvas(ground=False)
    probe = Canvas(ground=False)
    room = W - M - 36 - 8 - badge_font.getbbox("Hybrid Core Framework v%d" % VERSION)[2] - 12 - M
    for y, row in ((FOOT[0], hints), (FOOT[0] + HINT_ROW, second or [])):
        # the gaps shrink to keep the hints clear of the badge, whatever the font
        width = sum(hint_button(probe, F, family, a, 0, 0) + 4 + round(F.chip.getlength(w)) for a, w in row)
        gap = max(4, min(12, (room - width) // max(1, len(row) - 1)))
        x = M
        for action, what in row:
            x0 = x
            x += hint_button(c, F, family, action, x, y) + 4
            c.text((x, y + 9), what, F.chip, INK, anchor="lm")
            x += round(F.chip.getlength(what))
            if boxes is not None and (row is hints or action == "options"):
                boxes.append((x0, y, x, y + 18))
            x += gap
    return c.im


# The mouse (Danik, 2026-10-27): a pointer drawn over everything while the
# mouse moves, gone once the pad is used or after 5 seconds still. A click on a
# game puts the cursor on it, a double click opens Details; a click on a hint
# does what it says (Exit's question: its two), on a sort mode picks it; the
# right button is Back everywhere, the middle one Favourite (the list and
# Details); the wheel moves the cursor (in Details to the game before or
# after), and the scroll bar can be held and dragged. Options: a click on a page
# of the rail opens it, on a row puts the cursor on it, on the cursor's row takes
# the next value (About: opens), on its arrows the one before or after, on a
# switch flips it; the ports page: on a row puts the cursor on
# it, on the cursor's row or on a tickbox ticks it. POINTERS: its shapes, both
# in the text's colour with a rim against it, the player's choice in Options
# (Pointer: Danik, 2026-10-27, arrow or dart; drafts in the bar's colours not
# taken); layer_hover(): what a hint under it looks like.
POINTERS = {
    # the classic arrow
    "arrow": dict(shape=((0, 0), (0, 16), (4, 12.2), (6.8, 18.4), (9.4, 17.3), (6.7, 11.3), (11.6, 11.3))),
    # an arrowhead without its tail
    "dart": dict(shape=((0, 0), (0, 15.5), (4.6, 11.4), (11.4, 11.4))),
}
POINTER_SS = 4      # drawn that many times larger, then reduced: soft edges at its size
POINTER_TIP = 3     # the tip's place in its picture, from the left and the top
POINTER_SHADOW = (20, 0, 30)


def pointer_masks(style):
    """The pointer's coverage, each a picture of the same size with the tip at
    (POINTER_TIP, POINTER_TIP): its fill, its rim (a pixel round the fill, the
    fill too) and its shadow (the rim moved 1 right and 2 down, blurred, at 110
    of 255). The program colours them as layer_pointer() does (tools/make_assets.py
    writes them)"""
    s, t = POINTER_SS, POINTER_TIP
    pts = [(t * s + x * s, t * s + y * s) for x, y in POINTERS[style]["shape"]]
    w = -(-(int(max(x for x, y in pts)) + (t + 2) * s) // s) * s
    h = -(-(int(max(y for x, y in pts)) + (t + 3) * s) // s) * s
    shape = Image.new("L", (w, h), 0)
    ImageDraw.Draw(shape).polygon(pts, fill=255)
    rim = shape.filter(ImageFilter.MaxFilter(2 * s + 1))
    fill, rim = shape.reduce(s), rim.reduce(s)
    shadow = Image.new("L", rim.size, 0)
    shadow.paste(rim.point(lambda v: v * 110 // 255), (1, 2))
    return fill, rim, shadow.filter(ImageFilter.GaussianBlur(1.2))


def pointer_rim():
    """Against the fill (the text's colour): dark round a light one, light round a dark one"""
    return (0x14, 0x06, 0x1C) if sum(INK) > 3 * 128 else (0xF6, 0xF0, 0xFA)


def layer_pointer(style, xy, out=None):
    """The pointer, its tip at xy: the shadow, the rim, the fill, each laid over the one before"""
    fill, rim, shadow = pointer_masks(style)
    pic = Image.new("RGBA", fill.size, (0, 0, 0, 0))
    for colour, mask in ((POINTER_SHADOW, shadow), (pointer_rim(), rim), (INK, fill)):
        layer = Image.new("RGBA", fill.size, colour + (0,))
        layer.putalpha(mask)
        pic.alpha_composite(layer)
    out = out or blank()
    out.alpha_composite(pic, (round(xy[0]) - POINTER_TIP, round(xy[1]) - POINTER_TIP))
    return out


def hover_box(box):
    """Where a hint's hover is (inclusive), round its box"""
    x0, y0, x1, y1 = box
    return (x0 - 5, y0 - 3, x1 + 7, y1 + 3)


def layer_hover(box):
    """A hint under the pointer: a chip's ground behind it, as the sort modes
    have; the hint is laid over it again, where it is (clip_to())"""
    out = blank()
    ImageDraw.Draw(out).rounded_rectangle(hover_box(box), 12, fill=EDGE + (70,), outline=EDGE + (170,))
    return out


def pointer_screens(F, badge_font, ports):
    """Each pointer over the main screen (on a row of the list, and over a hint
    with HOVER), in three schemes, and a sheet of them at 3x"""
    screens, crops = {}, []
    for theme in ("purple", "lavender", "amber"):
        apply_theme(theme)
        m = Main(F, badge_font, ports, "A-Z", "doom2")
        L = m.frame_layers()
        boxes = []
        L["hints"] = layer_hints(F, MAIN_HINTS, badge_font, m.family, boxes, LIST_SECOND)
        under = compose(L)
        hint = boxes[1]   # Favourite
        hovered = under.copy()
        hovered.alpha_composite(layer_hover(hint))
        hovered.alpha_composite(clip_to(L["hints"], hover_box(hint)))
        row_xy = (LIST_X[0] + 150, LIST_Y[0] + LIST_PAD + 8 * ROW + 9)
        hint_xy = ((hint[0] + hint[2]) / 2 + 6, hint[1] + 11)
        for style in POINTERS:
            im = hovered.copy()
            layer_pointer(style, row_xy, im)
            layer_pointer(style, hint_xy, im)
            screens["pointer_%s_%s" % (theme, style)] = im.convert("RGB")
            for xy in (row_xy, hint_xy):
                box = (round(xy[0]) - 50, round(xy[1]) - 26, round(xy[0]) + 70, round(xy[1]) + 34)
                crops.append(im.crop(box).resize((360, 180), Image.NEAREST))
    apply_theme("purple")
    cols = 2 * len(POINTERS)
    sheet = Image.new("RGBA", (cols * 364, 3 * 184), (0, 0, 0, 255))
    for n, im in enumerate(crops):
        sheet.paste(im, ((n % cols) * 364, (n // cols) * 184))
    screens["pointer_sheet"] = sheet.convert("RGB")
    return screens


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


# B goes up to Systems (Danik, 2026-10-29), where it asks to exit ("Systems" did not fit beside the badge)
MAIN_HINTS = [("ok", "Details"), ("fav", "Favourite"), ("ports", "Repo"), ("back", "Back")]
LIST_SECOND = [("page", "Page"), ("layout", "Layout"), ("sort", "Hold: Shift"), ("options", "Options")]
# Select held (Danik, 2026-11-01): the legend says what the buttons do with it
# (a quick press of Select still changes the sort mode)
SHIFT_SECOND = [("sort", "Shift")]


def shift_hints(hidden=False):
    return [("fav", "Unhide" if hidden else "Hide")]


DETAILS_SECOND = [("page", "Previous / Next")]


class Main:
    """The main screen's state and its layers: the System Hybrid Ports' list"""
    title, chips, hints = "Hybrid Ports", CHIPS, MAIN_HINTS

    def __init__(self, F, badge_font, ports, mode, cursor_id, family="ps", art="shot", shift=False):
        self.F, self.badge_font, self.family, self.mode = F, badge_font, family, mode
        self.ports, self.art, self.shift = ports, art, shift
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

    def preview_logo(self, g, dx=0.0):
        return layer_preview_logo(g, dx)

    def preview_shot(self, g, xy=None):
        return layer_shot(self.F, g, xy or preview_shot_xy())

    def frame_layers(self):
        F = self.F
        g = self.game()
        return {
            "ground": layer_ground(), "logo": layer_logo(), "title": layer_title(F, self.title),
            "count": layer_count(F, self.count, self.title),
            "chips": layer_chips(F, self.family, chip_rects(F, self.chips)[self.mode], self.chips),
            "list_panel": layer_panel((LIST_X[0], LIST_Y[0], LIST_X[1], LIST_Y[1])),
            "prev_panel": layer_panel((PREV_X[0], LIST_Y[0], PREV_X[1], LIST_Y[1])),
            "rows": layer_rows(self.strips, len(self.rows), self.first, self.cursor),
            "prev_logo": blank() if self.art == "cover" else self.preview_logo(g),
            "prev_shot": layer_cover(g) if self.art == "cover" else self.preview_shot(g),
            "hints": (layer_hints(F, shift_hints(g["id"] in HIDDEN), self.badge_font, self.family, second=SHIFT_SECOND)
                      if self.shift else layer_hints(F, self.hints, self.badge_font, self.family, second=LIST_SECOND)),
            "badge": layer_badge(self.badge_font),
        }


ORDER = ["ground", "logo", "title", "count", "chips", "list_panel", "rows", "prev_panel", "prev_logo", "prev_shot",
         "hints", "badge"]


def compose(layers, order=ORDER):
    out = layers["ground"].copy() if "ground" in layers else blank()
    for name in order:
        if name != "ground" and name in layers:
            out.alpha_composite(layers[name])
    return out


def main_screen(F, badge_font, ports, mode, cursor_id, family="ps", art="shot", shift=False):
    return compose(Main(F, badge_font, ports, mode, cursor_id, family, art, shift).frame_layers()).convert("RGB")


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
    """The top right panel's content: the logo, the star if a favourite, the facts"""
    c = Canvas(ground=False)
    x0, y0, x1, y1 = D_INFO_PANEL
    pw = x1 - x0
    if logo:
        c.over(layer_details_logo(game))
    bx, by, bw, bh = details_logo_box()
    ty = by + bh + 8
    if game["id"] in FAVOURITES:
        c.star(x1 - 16, y0 + 16, 7, GOLD)
    layer = c.layer()
    ImageDraw.Draw(layer).line((x0 + 14, ty, x1 - 14, ty), fill=EDGE + (90,))
    c.over(layer)
    lw = max(F.small.getlength(k) for k, v, ink in details_facts(game, port)) + 12
    for n, (k, v, ink) in enumerate(details_facts(game, port)):
        y = ty + 8 + n * 16
        c.text((x0 + 14, y + 1), k, F.small, DIM)
        c.text((x0 + 14 + lw, y), clip(v, F.chip, pw - 28 - lw), F.chip, ink)
    return c.im


DLOGO = (252, 77)   # a logo's size at most (.dlogo): as wide as the box at the largest Screen margin, to fit at every one


def details_logo_box():
    x0, y0, x1, y1 = D_INFO_PANEL
    return x0 + 12, y0 + 10, x1 - x0 - 24, DLOGO[1]


def details_logo_xy(logo):
    bx, by, bw, bh = details_logo_box()
    return bx + (bw - logo.width) // 2, by + (bh - logo.height) // 2


def layer_details_logo(game):
    logo = logo_picture(game["logo"], *DLOGO)
    out = blank()
    out.alpha_composite(logo, details_logo_xy(logo))
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
    return [("ok", "Play"), ("fav", "Favourite"), ("ports", "Repo"), ("back", "Back")]


def details_layers(F, badge_font, game, port, family="ps"):
    return {"ground": layer_ground(), "logo": layer_logo(), "title": layer_title(F), "count": layer_count(F, "Details"),
            "shot_panel": layer_panel(D_SHOT_PANEL), "shot": layer_shot(F, game, D_SHOT),
            "info_panel": layer_panel(D_INFO_PANEL), "info": layer_details_info(F, game, port),
            "desc_panel": layer_panel(D_DESC_PANEL), "desc": layer_details_desc(F, game),
            "hints": layer_hints(F, details_hints(game), badge_font, family, second=DETAILS_SECOND), "badge": layer_badge(badge_font)}


D_ORDER = ["ground", "logo", "title", "count", "shot_panel", "shot", "info_panel", "info", "desc_panel", "desc",
           "hints", "badge"]


def details_screen(F, badge_font, ports, game_id, family="ps"):
    m = Main(F, badge_font, ports, "A-Z", game_id, family)
    return compose(details_layers(F, badge_font, m.game(), m.port(m.game()), family), D_ORDER).convert("RGB")


def header(c, F, mode, count, family, title="Hybrid Ports"):
    c.over(layer_logo())
    c.over(layer_title(F, title))
    c.over(layer_count(F, count, title))
    if mode:
        c.over(layer_chips(F, family, chip_rects(F)[mode]))


def footer(c, F, hints, badge_font, family, second=None):
    c.over(layer_hints(F, hints, badge_font, family, second=second))
    c.over(layer_badge(badge_font))


PORTS_LIST_X = (M, 400)   # the ports page's list; the port's panel to the right of it


def ports_logo_box():
    """The box a game's logo is fitted into on the ports page (its panel's width, less 20)"""
    return W - M - (PORTS_LIST_X[1] + 12) - 20, 80


def ports_screen(F, badge_font, ports, cursor_engine, family="ps"):
    c = Canvas()
    header(c, F, None, "12 ports", family, "Repo")   # its own title, as Options (Danik, 2026-11-02)
    rows = [("set", "Launcher", "On"), ("set", "Menu entries", "Other, as dnk-<Title>")]
    for p in sorted(ports, key=lambda p: p["engine"].lower()):
        rows.append(("port", p["engine"], p["engine"] in INSTALLED, p))
        for g in p["games"]:
            rows.append(("game", g["title"], p["engine"] in INSTALLED, p))
    cursor = next(n for n, r in enumerate(rows) if r[0] == "port" and r[1] == cursor_engine)
    x0, x1 = PORTS_LIST_X
    y0, y1 = MAIN
    c.panel((x0, y0, x1, y1))
    n_rows = (y1 - y0 - 12) // ROW
    for n, row in enumerate(rows[:n_rows]):
        kind, label, val = row[:3]
        y = y0 + 6 + n * ROW
        mid = y + ROW / 2
        sel = n == cursor
        if sel:
            c.bar((x0 + 1, y + 1, x1 - SB_W, y + ROW - 1), panel=(x0, y0, x1, y1))
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
    logo = logo_picture(port["games"][0]["logo"], *ports_logo_box())
    c.picture(logo, (px0 + (pw - logo.width) // 2, y0 + 10 + 80 - logo.height))
    y = y0 + 104
    c.text((px0 + 14, y), port["engine"], F.title, INK)
    c.pill((px0 + 14, y + 28), port["version"], F.small, CYAN, SEL_INK, h=18)
    c.text((px0 + 14, y + 58), "%d games" % len(port["games"]), F.text, DIM)
    c.text((px0 + 14, y + 80), "Installed" if port["engine"] in INSTALLED else "Not installed", F.text,
           GOLD if port["engine"] in INSTALLED else DIM)
    footer(c, F, [("ok", "Tick"), ("fav", "Apply"), ("back", "Back")], badge_font, family, [("page", "Page")])
    return c.im.convert("RGB")


OPTIONS = [  # the frontend's own settings, kept in games/dnk/dnkLauncher/settings.txt
    ("Font", "font", "The font of every screen here. Chakra Petch unless you pick another."),
    ("Palette", "colours", "The colours of every screen here. MiSTer Purple unless you pick another."),
    ("Pattern", "pattern", "A pattern over the colours, moving if you like. Checker unless you pick another; "
                           "Off leaves the colours alone."),
    ("Pattern intensity", "intensity", "How much darker the pattern's squares, lines or dots are than the "
                                       "colours under them. 25% unless you pick another."),
    ("Pattern strength", "strength", "Where the pattern shows: as much everywhere, or strongest at the top, the "
                                     "bottom or the edges of the screen, fading away from there."),
    ("Pattern direction", "direction", "Which way the pattern moves."),
    ("Pattern speed", "speed", "How fast the pattern moves: Normal is 30 pixels a second. Still stops it."),
    ("Flicker filter", "filter", "On a 15kHz screen 640x480 is interlaced: the filter softens what would blink, "
                                 "Light a little, Strong fully. The OSD's Flicker Filter is the same."),
    ("Screen margin", "margin", "Moves everything in from the screen's edges, for a CRT that hides them, without "
                                "changing the picture's shape as CRT Options in the OSD do."),
    ("Button symbols", "buttons", "Auto shows the buttons of the controller you use, when it is a Switch, "
                                  "Xbox or PlayStation one. Pick one for any other."),
    ("Pointer", "pointer", "The mouse's pointer, shown while the mouse moves: an arrow or a dart. The arrow "
                           "unless you pick the other."),
    ("Layout", "layout", "How the games are shown: a list beside the selected game's picture, or a carousel of "
                         "their pictures. L2 and R2 change it at any time."),
    ("Game art", "art", "What a game shows in the list and the carousel: its logo and screenshot, or its cover. "
                        "A game with no cover keeps its logo and screenshot."),
    ("Sort games", "sort", "How the list is sorted when the frontend starts. Select changes it at any time."),
    ("Start in", "start", "Where the launcher opens: the System you were in last, on the game you were on, "
                          "or the Systems screen. The last System unless you pick the other."),
    ("Show hidden", "hidden", "On shows what you hid, dimmed and marked, so that you can unhide it: hold Select "
                              "and press Favourite. It stays hidden when this is off."),
    ("Show missing cores", "missing", "Off shows only what is on this MiSTer. On adds the other cores of the MiSTer "
     "database at the bottom of Systems, greyed out, to see what there is."),
    ("System logos", "logos", "One colour draws the logos on Systems in the screens' text colour. In colour shows "
     "each logo's own colours, where it has them. One colour unless you pick the other."),
    ("About", "about", "The launcher's version, its credits and the Patreon."),
]
# Options' page Game lists (approved by Danik, 2026-11-02): the consoles' lists
GAME_LIST_OPTIONS = [
    ("Game names", "names", "How a console's games are named in its list: the title alone (Super Metroid), the "
                            "title and its region, or the file's whole name."),
    ("First region", "region", "Where a game has several versions, the one its row and A start: the others are in "
                               "its Details. USA unless you pick another."),
    ("Show betas and hacks", "extras", "On adds what the lists leave out: betas, prototypes, demos and hacks, as "
                                       "No-Intro names them. BIOS files never show."),
    ("Look for games", "scan", "Goes through the consoles' folders on the SD card and USB drives, inside zips too, "
                               "and makes their lists again. The launcher stays usable meanwhile."),
    ("Download game art", "artdl", "Opens the consoles found: tick those whose boxes, logos and screenshots to fetch "
                                   "from libretro-thumbnails, only what is new or missing. Chosen while it runs: cancels it."),
]
GAME_LIST_ACTIONS = ("scan", "artdl")
OPTIONS += GAME_LIST_OPTIONS
# Danik's design of 2026-11-01 (the workbench's Side rail): the pages down the
# left, their rows on the right, both in sight. The page follows the rail's
# cursor; Right or OK goes into the rows, Back one level up; L1 / R1 the page
# wherever the cursor is.
OPTION_PAGES = (
    ("Theme", "palette", ("font", "colours")),
    ("Background", "wave", ("pattern", "intensity", "strength", "direction", "speed")),
    ("Screen", "screen", ("filter", "margin")),
    ("Controls", "pad", ("buttons", "pointer")),
    ("Game lists", "cart", tuple(o[1] for o in GAME_LIST_OPTIONS)),
    ("Library", "list", ("layout", "art", "sort", "start", "hidden", "missing", "logos", "about")),
)
OPTION_BY_KEY = {o[1]: o for o in OPTIONS}
OPT_GREYED = ("intensity", "strength", "direction", "speed")   # with Pattern Off: greyed out, the cursor passes them
OPT_SWITCHES = ("hidden", "missing", "extras")   # Off and On: a switch
OPT_ACTIONS = GAME_LIST_ACTIONS                  # rows that do something when chosen: what they did last where a value goes
OPT_GREY_ALPHA = 110
OPT_ROW = 34
OPT_HELP = (MAIN[1] - 42, MAIN[1])      # the help box under the list: 2 lines of the small font
OPT_LIST = (MAIN[0], OPT_HELP[0] - 10)
OPT_ROWS = (OPT_LIST[1] - OPT_LIST[0] - 20) // OPT_ROW
OPT_RAIL_W = 150                        # the rail's panel, from M; the rows' panel 10 to its right
OPT_RAIL_ROW = 30
OPT_RAIL_BAR = 26
OPT_X0 = M + OPT_RAIL_W + 10
OPT_FADE = 11                           # a page's rows and help coming in (180 ms)
ICON_SIZE = 18                          # option_icon_mask(): its centre at 9, 9
ICON_SS = 8


def _arc(cx, cy, r, a0, a1, n=48):
    return [(cx + r * math.cos(a0 + (a1 - a0) * i / n), cy + r * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]


def _quad(p0, p1, p2, n=24):
    return [tuple((1 - t) ** 2 * a + 2 * t * (1 - t) * b + t * t * c for a, b, c in zip(p0, p1, p2))
            for t in (i / n for i in range(n + 1))]


def _cubic(p0, p1, p2, p3, n=24):
    return [tuple((1 - t) ** 3 * a + 3 * t * (1 - t) ** 2 * b + 3 * t * t * (1 - t) * c + t ** 3 * d
                  for a, b, c, d in zip(p0, p1, p2, p3)) for t in (i / n for i in range(n + 1))]


def _rrect(x0, y0, x1, y1, r):
    return (_arc(x1 - r, y0 + r, r, -math.pi / 2, 0, 8) + _arc(x1 - r, y1 - r, r, 0, math.pi / 2, 8) +
            _arc(x0 + r, y1 - r, r, math.pi / 2, math.pi, 8) + _arc(x0 + r, y0 + r, r, math.pi, 1.5 * math.pi, 8))


def option_icon_mask(name):
    """A page's icon on the rail, drawn in a square of 24 units from -12 to 12
    and shown 16 pixels across: the coverage, coloured as the rail's text (the
    workbench's icon(); tools/make_assets.py writes them)"""
    s = ICON_SS * ICON_SIZE / 24 * 24 / 27
    k = 16 / 24 * ICON_SS   # a unit in the supersampled picture
    size = ICON_SIZE * ICON_SS
    im = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(im)
    width = 2.2 * k

    def line(*pts, closed=False):
        p = [(size / 2 + x * k, size / 2 + y * k) for x, y in pts]
        if closed:
            p.append(p[0])
        d.line(p, fill=255, width=round(width), joint="curve")
        for x, y in (p[0], p[-1]):
            d.ellipse((x - width / 2, y - width / 2, x + width / 2, y + width / 2), fill=255)

    def dot(x, y, r=1.6):
        cx, cy, q = size / 2 + x * k, size / 2 + y * k, r * k
        d.ellipse((cx - q, cy - q, cx + q, cy + q), fill=255)

    if name == "palette":
        arc = _arc(0, 0, 10, 0.4, 5.9)
        line(*(arc + _quad(arc[-1], (3, -2), (3, 3)) + _quad((3, 3), (3, 7), (9, 4))))
        for x, y in ((-5, -3), (0, -6), (-5, 3), (4, -5)):
            dot(x, y)
    elif name == "wave":
        for dy in (-6, 0, 6):
            line(*(_cubic((-10, dy), (-5, dy - 6), (-3, dy + 6), (0, dy)) + _cubic((0, dy), (3, dy - 6), (5, dy + 6), (10, dy))))
    elif name == "screen":
        line(*_rrect(-10, -8, 10, 6, 2), closed=True)
        line((-4, 10), (4, 10))
        line((0, 6), (0, 10))
    elif name == "pad":
        line(*_rrect(-11, -6, 11, 7, 6), closed=True)
        line((-7, 0.5), (-3, 0.5))
        line((-5, -1.5), (-5, 2.5))
        dot(4, -1)
        dot(7, 2)
    elif name == "cart":   # a cartridge: its label and its contacts
        line(*_rrect(-8, -11, 8, 9, 2), closed=True)
        line(*_rrect(-4.5, -7, 4.5, 1, 1), closed=True)
        for x in (-4, 0, 4):
            line((x, 9), (x, 11))
    elif name == "list":
        for dy in (-7, 0, 7):
            dot(-9, dy)
            line((-5, dy), (10, dy))
    return im.reduce(ICON_SS)


OPT_ICONS = tuple(p[1] for p in OPTION_PAGES)


def page_options(page, hide=()):
    """The rows of a page, without those this MiSTer has no use for"""
    return [OPTION_BY_KEY[k] for k in OPTION_PAGES[page][2] if k not in hide]


def option_grey(option, values):
    return option[1] in OPT_GREYED and values.get("pattern") == "Off"


def options_first(cursor, n):
    """The first option in sight with the cursor come down from the top: the
    list scrolls when the bar comes within two rows of its edge, as the game
    list (and the bar glides, as the game list's: anim_cursor())"""
    return max(0, min(cursor - (OPT_ROWS - 3), n - OPT_ROWS))


def option_row(c, F, fonts, values, option, x0, x1, y, sel):
    """An option's row from y: its label and its value with the arrows, or its
    switch; greyed out where Pattern is Off; About its label alone"""
    label, key, help_text = option
    mid = y + 15
    grey = option_grey(option, values)
    if grey:
        c.text((x0 + 14, mid), label, F.row, DIM, anchor="lm", alpha=OPT_GREY_ALPHA)
    else:
        c.text((x0 + 14, mid), label, F.row, SEL_INK if sel else INK, anchor="lm")
    if key == "about" or key in OPT_ACTIONS:   # opens, or does something: its label alone
        return
    value = values.get(key, "Off")
    if key in OPT_SWITCHES:
        on = value == "On"
        a, b = x1 - 58, x1 - 24
        ss = 4
        layer = Image.new("RGBA", ((b - a + 1) * ss, 17 * ss), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        track = (SEL_INK if sel else CYAN) + (255,) if on else (SEL_INK if sel else DIM) + (90,)
        d.rounded_rectangle((0, 0, (b - a) * ss, 16 * ss), 8 * ss, fill=track)
        kx = (b - a - 8 if on else 8) * ss
        knob = PINK if sel else PANEL if on else INK
        d.ellipse((kx - 6 * ss, 2 * ss, kx + 6 * ss, 14 * ss), fill=knob + (255,))
        over = c.layer()
        over.paste(layer.reduce(ss), (a, mid - 8))
        c.over(over)
        return
    vf = fonts[value][1] if key == "font" else F
    vtext = fonts[value][0] if key == "font" else THEMES[value]["label"] if key == "colours" else value
    vx = x1 - 34 - round(vf.row.getlength(vtext))
    if grey:
        c.text((vx, mid), vtext, vf.row, DIM, anchor="lm", alpha=OPT_GREY_ALPHA)
    else:
        c.text((vx, mid), vtext, vf.row, SEL_INK if sel else CYAN, anchor="lm")
    layer = c.layer()     # the arrows: left and right change it
    d = ImageDraw.Draw(layer)
    a = DIM + (OPT_GREY_ALPHA,) if grey else (SEL_INK if sel else DIM) + (255,)
    d.polygon((vx - 14, mid, vx - 8, mid - 5, vx - 8, mid + 5), fill=a)
    d.polygon((x1 - 20, mid, x1 - 26, mid - 5, x1 - 26, mid + 5), fill=a)
    c.over(layer)


def rail_row(c, F, n, x0, y, sel):
    """A page on the rail: its icon and its name"""
    name, icon, _ = OPTION_PAGES[n]
    ink = SEL_INK if sel else INK
    mask = option_icon_mask(icon)
    pic = Image.new("RGBA", mask.size, ink + (0,))
    pic.putalpha(mask)
    layer = c.layer()
    layer.paste(pic, (x0 + 20 - ICON_SIZE // 2, y + 13 - ICON_SIZE // 2))
    c.over(layer)
    c.text((x0 + 34, y + 13), name, F.chip, ink, anchor="lm")


def layer_options_rail(F, page, focus):
    """The pages: the bar's colours on the page while the cursor is on the rail,
    the rim's colour, faint, while it is in the rows"""
    c = Canvas(ground=False)
    x0, x1 = M, M + OPT_RAIL_W
    y0, y1 = OPT_LIST
    box = (x0, y0, x1, y1)
    c.panel(box)
    for n in range(len(OPTION_PAGES)):
        y = y0 + 10 + n * OPT_RAIL_ROW
        sel = focus == "rail" and n == page
        if sel:
            c.bar((x0 + 1, y, x1 - 1, y + OPT_RAIL_BAR), panel=box)
        elif n == page:
            layer = c.layer()
            ImageDraw.Draw(layer).rectangle((x0 + 1, y, x1 - 2, y + OPT_RAIL_BAR - 1), fill=EDGE + (60,))
            c.over(clip_to_panel(layer, box))
        rail_row(c, F, n, x0, y, sel)
    return c.im


def layer_options_rail_glide(F, bar):
    """The rail with the cursor's bar on row bar (a fraction while it glides)"""
    c = Canvas(ground=False)
    x0, x1 = M, M + OPT_RAIL_W
    y0, y1 = OPT_LIST
    c.panel((x0, y0, x1, y1))
    under = Canvas(ground=False)
    top = y0 + 10
    under.bar((x0 + 1, top, x1 - 1, top + len(OPTION_PAGES) * OPT_RAIL_ROW))
    for n in range(len(OPTION_PAGES)):
        rail_row(c, F, n, x0, top + n * OPT_RAIL_ROW, False)
        rail_row(under, F, n, x0, top + n * OPT_RAIL_ROW, True)
    by = top + round(bar * OPT_RAIL_ROW)
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rectangle((x0 + 1, by, x1 - 2, by + OPT_RAIL_BAR - 1), fill=255)
    c.im.paste(under.im.convert("RGBA"), (0, 0), mask)
    return c.im


def layer_options_rows_panel():
    c = Canvas(ground=False)
    c.panel((OPT_X0, OPT_LIST[0], W - M, OPT_LIST[1]))
    return c.im


def layer_options_list(F, fonts, cursor, values, options, focus="list"):
    """The page's rows, no bar while the cursor is on the rail; a scroll bar
    once there are more than fit"""
    c = Canvas(ground=False)
    x0, x1 = OPT_X0, W - M
    y0, y1 = OPT_LIST
    first = options_first(cursor, len(options))
    for n in range(first, min(len(options), first + OPT_ROWS)):
        y = y0 + 10 + (n - first) * OPT_ROW
        sel = focus == "list" and n == cursor
        if sel:
            c.bar((x0 + 1, y, x1 - SB_W, y + 30), panel=(x0, y0, x1, y1))
        option_row(c, F, fonts, values, options[n], x0, x1, y, sel)
    c.over(scroll_bar((x0, y0, x1, y1), OPT_ROWS, len(options), first))   # as the game list's
    return c.im


def option_strips(F, fonts, values, options):
    """The options one under the other, as they are and as they are under the
    cursor's bar (its colours without a break: layer_option_rows() cuts the
    bar out where it is), as the game list's strips"""
    w = W - M - OPT_X0
    out = []
    for sel in (False, True):
        c = Canvas(ground=False, size=(w, len(options) * OPT_ROW))
        if sel:
            c.bar((1, 0, w - SB_W, c.im.height))
        for n, option in enumerate(options):
            option_row(c, F, fonts, values, option, 0, w, n * OPT_ROW, sel)
        out.append(c.im)
    return out


def layer_option_rows(strips, n_total, scroll, bar):
    """The options from row `scroll` on and the bar on row `bar` (both
    fractions while they move), as layer_rows() the game list's: Options'
    list is its panel and these over it"""
    normal, selected = strips
    x0, x1 = OPT_X0, W - M
    y0, y1 = OPT_LIST
    w, h = x1 - x0, OPT_ROWS * OPT_ROW
    top = round(scroll * OPT_ROW)
    view = normal.crop((0, top, w, top + h))
    by = round((bar - scroll) * OPT_ROW)
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rectangle((1, by, w - SB_W - 1, by + 29), fill=255)
    view.paste(selected.crop((0, top, w, top + h)), (0, 0), mask)
    out = blank()
    out.paste(view, (x0, y0 + 10))
    out = clip_to(out, (x0 + 1, y0 + 2, x1 - 1, y1 - 2))
    out = clip_to_panel(out, (x0, y0, x1, y1))
    out.alpha_composite(scroll_bar((x0, y0, x1, y1), OPT_ROWS, n_total, scroll))
    return out


def options_help_text(page, cursor, options, focus):
    if focus == "rail":   # on the rail: what the page has
        return ", ".join(o[0] for o in options if o[1] != "about") + "."
    return options[cursor][2]


def layer_options_help_panel():
    c = Canvas(ground=False)
    c.panel((M, OPT_HELP[0], W - M, OPT_HELP[1]))
    return c.im


def layer_options_help_text(F, help_text):
    """What the option under the cursor does, or what the page has: one or two lines of the small font"""
    c = Canvas(ground=False)
    x0, x1 = M, W - M
    y0, y1 = OPT_HELP
    lines = wrap(help_text, F.small, x1 - x0 - 28)
    assert len(lines) <= 2, (F.label, help_text, lines)
    top = y0 + (y1 - y0 - len(lines) * 15) // 2
    for n, line in enumerate(lines):
        c.text((x0 + 14, top + n * 15), line, F.small, DIM)
    return c.im


# About (the workbench's choice; its words mine, Danik may change them): over
# the rows and the help, the rail staying
ABOUT = (OPT_X0, MAIN[0], W - M, MAIN[1])
ABOUT_LINES = (
    ("head", "dnkLauncher", INK, 28),
    ("row", "The frontend of the hybrid ports", INK, 22),
    ("row", "Hybrid Core Framework v%d" % VERSION, CYAN, 22),
    ("row", "By Danik: patreon.com/itsdanik", CYAN, 32),
    ("small", "Inspired by MiSTer Frontier by MiSTer Organize. Thank you!", DIM, 15),
    ("gap", "", DIM, 8),
    ("small", "Fonts: Chakra Petch, Nunito, Atkinson Hyperlegible, Rubik and Lexend, under the SIL Open Font "
              "License.", DIM, 15),
    ("gap", "", DIM, 8),
    ("small", "System logos: es-theme-carbon by Rookervik, based on simple by Nils Bonenberger, CC BY-NC-SA 2.0; "
              "tty2oled+; Wikimedia Commons. The logos are trademarks of their owners.", DIM, 15),
)


def layer_options_about(F):
    c = Canvas(ground=False)
    x0, y0, x1, y1 = ABOUT
    c.panel(ABOUT)
    y = y0 + 14
    for kind, text, colour, step in ABOUT_LINES:
        if kind == "gap":
            y += step
            continue
        font = {"head": F.head, "row": F.row, "small": F.small}[kind]
        for line in wrap(text, font, x1 - x0 - 28):
            c.text((x0 + 14, y), line, font, colour)
            y += step
    assert y <= y1 - 6, (F.label, y, y1)
    return c.im


OPTION_HINTS_RAIL = [("ok", "Open"), ("back", "Back")]
OPTION_HINTS = [("ok", "Change"), ("back", "Back")]
OPTION_SECOND = [("page", "Previous / Next page")]


def option_hints(options, cursor, focus):
    if focus == "about":
        return [("back", "Back")]
    if focus == "rail" or options[cursor][1] == "about":
        return OPTION_HINTS_RAIL
    return OPTION_HINTS


def options_layers(F, badge_font, fonts, page, cursor, values, family="ps", focus="list", hide=()):
    options = page_options(page, hide)
    layers = {"ground": layer_ground(), "logo": layer_logo(), "title": layer_title(F, "Options"),
              "rail": layer_options_rail(F, page, "list" if focus == "about" else focus),
              "hints": layer_hints(F, option_hints(options, cursor, focus), badge_font, family,
                                   second=None if focus == "about" else OPTION_SECOND),
              "badge": layer_badge(badge_font)}
    if focus == "about":
        layers["about"] = layer_options_about(F)
    else:
        layers["panel"] = layer_options_rows_panel()
        layers["rows"] = layer_options_list(F, fonts, cursor, values, options, focus)
        layers["help_panel"] = layer_options_help_panel()
        layers["help"] = layer_options_help_text(F, options_help_text(page, cursor, options, focus))
    return layers


OPTIONS_ORDER = ["logo", "title", "rail", "panel", "rows", "about", "help_panel", "help", "hints", "badge"]


def options_screen(F, badge_font, fonts, page, cursor, values, family="ps", focus="list", hide=()):
    """fonts: the Fonts of every choice, the font row's value shown in itself"""
    L = options_layers(F, badge_font, fonts, page, cursor, values, family, focus, hide)
    return compose(L, [k for k in OPTIONS_ORDER if k in L]).convert("RGB")


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
    small = logo_picture(g["logo"], *DLOGO)
    l1 = details_logo_xy(small) + (small.width, small.height)
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
    """Start: the main screen's panels sink and go, Options' rise, the cursor
    on the rail's first page; the header's System goes, "Options" comes"""
    m = Main(F, badge_font, ports, "A-Z", "doom2")
    L = m.frame_layers()
    O = options_layers(F, badge_font, fonts, 0, 0, values, m.family, "rail")
    base = compose({k: L[k] for k in ("ground", "logo", "badge")}, ["logo", "badge"])
    left = compose({"ground": blank(), "rail": O["rail"], "panel": O["panel"], "rows": O["rows"]}, ["rail", "panel", "rows"])
    right = compose({"ground": blank(), "help_panel": O["help_panel"], "help": O["help"]}, ["help_panel", "help"])
    leaving = [(["list_panel", "rows"], 0, 7), (["prev_panel", "prev_logo", "prev_shot"], 1, 8)]
    frames = []
    for f in range(20):
        fr = base.copy()
        fr.alpha_composite(place(L["title"], alpha=1 - span(f, 0, 6)))
        fr.alpha_composite(place(L["chips"], alpha=1 - span(f, 0, 6)))
        fr.alpha_composite(place(L["count"], alpha=1 - span(f, 0, 6)))
        fr.alpha_composite(place(O["title"], alpha=span(f, 5, 12)))
        fr.alpha_composite(place(L["hints"], alpha=1 - span(f, 0, 6)))
        fr.alpha_composite(place(O["hints"], alpha=span(f, 6, 13)))
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


def anim_options_cursor(F, badge_font, ports, fonts, values):
    """Down the Library page from its sixth row to its last: the bar glides, the
    list scrolls when the bar comes within two rows of its edge, as the game
    list's (anim_cursor()); what the option does changes at once"""
    page = len(OPTION_PAGES) - 1
    options = page_options(page)
    O = options_layers(F, badge_font, fonts, page, 0, values, focus="list")
    fixed = compose({k: O[k] for k in ("ground", "logo", "title", "rail", "panel", "help_panel")},
                    ["logo", "title", "rail", "panel", "help_panel"])
    strips = option_strips(F, fonts, values, options)
    n = len(options)
    stops = [(c, options_first(c, n)) for c in range(5, n)]
    frames = []
    for (c0, f0), (c1, f1) in zip(stops, stops[1:]):
        hints = layer_hints(F, option_hints(options, c1, "list"), badge_font, "ps", second=OPTION_SECOND)
        for f in range(14):
            e = ease_out(span(f, 0, 7))
            fr = fixed.copy()
            fr.alpha_composite(layer_option_rows(strips, n, lerp(f0, f1, e), lerp(c0, c1, e)))
            for layer in (layer_options_help_text(F, options[c1][2]), hints, O["badge"]):
                fr.alpha_composite(layer)
            frames.append(fr.convert("RGB"))
    return frames


def anim_options_page(F, badge_font, fonts, values):
    """Down the rail, from a page to the next: the bar glides; the new page's
    rows and its help come in over OPT_FADE fields, the panels staying"""
    O = options_layers(F, badge_font, fonts, 0, 0, values, focus="rail")
    N = options_layers(F, badge_font, fonts, 1, 0, values, focus="rail")
    fixed = compose({k: O[k] for k in ("ground", "logo", "title", "panel", "help_panel", "hints", "badge")},
                    ["logo", "title", "panel", "help_panel", "hints", "badge"])
    frames = []
    for f in range(OPT_FADE + 2):
        fr = fixed.copy()
        fr.alpha_composite(layer_options_rail_glide(F, ease_out(span(f, 0, 7))))
        p = ease_out(span(f, 0, OPT_FADE))
        fr.alpha_composite(place(N["rows"], alpha=p))
        fr.alpha_composite(place(N["help"], alpha=p))
        frames.append(fr.convert("RGB"))
    return frames


# Exit: Back on the list asks first (Danik, 2026-10-27). The screen dims, a
# panel in the middle asks; OK leaves to the MiSTer menu, Back stays.
CONFIRM = (150, 180, W - 150, 300)   # the panel
CONFIRM_DIM = 120                     # the black over the screen behind it
CONFIRM_ALPHA = 245                   # its body, all but opaque: nothing of the list shows through
CONFIRM_FIELDS = 10


def layer_confirm(F, family="ps"):
    """The question and its two buttons on a panel"""
    c = Canvas(ground=False)
    x0, y0, x1, y1 = CONFIRM
    c.panel(CONFIRM, alpha=CONFIRM_ALPHA)
    c.text(((x0 + x1) / 2, y0 + 38), "Exit to the MiSTer menu?", F.head, INK, anchor="mm")
    where = places(family)
    hints = (("ok", "Exit"), ("back", "Stay"))
    probe = Canvas(ground=False)
    width = sum(button(probe, F, family, where[a], 0, 0) + 4 + round(F.chip.getlength(w)) for a, w in hints) + 24
    x, y = round((x0 + x1 - width) / 2), y0 + 70
    for action, what in hints:
        x += button(c, F, family, where[action], x, y) + 4
        c.text((x, y + 9), what, F.chip, INK, anchor="lm")
        x += round(F.chip.getlength(what)) + 24
    return c.im


def confirm_frame(main, panel, f):
    """The main screen with the question coming up, field f of CONFIRM_FIELDS"""
    fr = main.copy()
    dim = span(f, 0, 8)
    if dim > 0:
        fr.alpha_composite(Image.new("RGBA", (W, H), (0, 0, 0, round(CONFIRM_DIM * dim))))
    p = ease_out(span(f, 2, CONFIRM_FIELDS))
    if p > 0:
        fr.alpha_composite(place(panel, 0, 10 * (1 - p), p))
    return fr.convert("RGB")


# While the launcher says what is installed and what there is (its table, 0.4
# seconds on the MiSTer, 3 to 5 before launcher 9's norm()), before the ports page: a panel as the question's,
# without buttons (Danik, 2026-10-27). It comes up as the question does; the
# page then opens over it as it fades with the main screen's panels.
WAIT = (170, 200, W - 170, 280)


def layer_wait(F):
    """Please wait, on a panel"""
    c = Canvas(ground=False)
    x0, y0, x1, y1 = WAIT
    c.panel(WAIT, alpha=CONFIRM_ALPHA)
    c.text(((x0 + x1) / 2, y0 + 30), "Please wait...", F.head, INK, anchor="mm")
    c.text(((x0 + x1) / 2, y0 + 58), "Reading what is installed and available", F.small, DIM, anchor="mm")
    return c.im


def wait_screen(F, badge_font, ports):
    main = compose(Main(F, badge_font, ports, "A-Z", "doom2").frame_layers())
    return confirm_frame(main, layer_wait(F), CONFIRM_FIELDS)


def anim_wait(F, badge_font, ports):
    main = compose(Main(F, badge_font, ports, "A-Z", "doom2").frame_layers())
    panel = layer_wait(F)
    return [confirm_frame(main, panel, f) for f in range(CONFIRM_FIELDS)]


def confirm_screen(F, badge_font, ports, family="ps"):
    main = compose(Main(F, badge_font, ports, "A-Z", "doom2", family).frame_layers())
    return confirm_frame(main, layer_confirm(F, family), CONFIRM_FIELDS)


def anim_confirm(F, badge_font, ports):
    main = compose(Main(F, badge_font, ports, "A-Z", "doom2").frame_layers())
    panel = layer_confirm(F)
    return [confirm_frame(main, panel, f) for f in range(CONFIRM_FIELDS)]


def anim_play(F, badge_font, ports):
    """A in Details: the screenshot's frame flashes, the screenshot grows to
    the whole screen while the rest goes dark, then black; then the game's
    core is loaded. The screenshot grows by whole pixels, blocky (Danik,
    2026-10-27: "I don't care if the pixels are blocky"): mixing them, as it
    did until then, was too slow on the MiSTer's ARM every field"""
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
            fr.paste(src.resize((round(w), round(h)), Image.NEAREST), (round(lerp(sx, 0, t)), round(lerp(sy, 0, t))))
        out = span(f, 24, 36)
        if out > 0:
            fr.alpha_composite(Image.new("RGBA", (W, H), (0, 0, 0, round(255 * out))))
        frames.append(fr.convert("RGB"))
    return frames


# Systems (Danik, 2026-10-29; docs/dnklauncher.md, "Systems"): a screen above
# the game list with a card for each System, Hybrid Ports and ScummVM, each a
# picture of the screenshot's size with the System's name in it
# (assets/systems/ of the dnkLauncher repository); ScummVM only when its core
# is installed. The ScummVM System lists the ScummVM port's own entry first,
# then the games marked as favourites in ScummVM: a card each (its icon,
# 192x192 in the middle of 256x192 and clear around it) and the facts ScummVM
# knows, no logo and no description. Its sort modes: SV_CHIPS. Picked by Danik
# on the mock-ups' review page, round 1 (2026-10-29).

# the cards are dnkLauncher's: its repository is the one this hybrid/ is in, or ~/MiSTer-dnkLauncher
SYSTEMS_DIR = next(d for d in (os.path.join(os.path.dirname(root), "assets", "systems"),
                               os.path.expanduser("~/MiSTer-dnkLauncher/assets/systems")) if os.path.isdir(d))
SYSTEMS = [("hybrid", "Hybrid Ports"), ("scummvm", "ScummVM")]
# Its sort modes: By engine, By publisher and By year (Danik, 2026-10-29), named without "By": five with
# "By" reach the MiSTer logo (CREATE at x 182 to 201 by the font, the logo ends at 240); without it 242 to 257
SV_CHIPS = ["A-Z", "Engine", "Publisher", "Year", "Recent"]
# Repo on every screen but Options and the Repo page itself (Danik, 2026-10-31)
SV_HINTS = [("ok", "Details"), ("ports", "Repo"), ("back", "Back")]
SYS_HINTS = [("ok", "Open"), ("ports", "Repo"), ("back", "Exit")]
SV_ICON = 192             # a ScummVM game's icon on its card
SV_ICONS = os.path.expanduser("~/MiSTer-scummvm/build/icons")   # the packs (its tools/icons.sh)

# What the mock-ups show of a player's favourites: target, game id, engine id,
# platform, language, last played
SV_FAVOURITES = [
    ("monkey-cd", "monkey", "scumm", "DOS", "English", "today"),
    ("monkey2", "monkey2", "scumm", "DOS", "English", "3 days ago"),
    ("comi", "comi", "scumm", "Windows", "English", "last week"),
    ("atlantis", "atlantis", "scumm", "DOS", "English", "yesterday"),
    ("tentacle", "tentacle", "scumm", "DOS", "English", "2 weeks ago"),
    ("samnmax", "samnmax", "scumm", "DOS", "German", "never"),
    ("ft", "ft", "scumm", "DOS", "English", "never"),
    ("dig", "dig", "scumm", "DOS", "English", "a month ago"),
    ("sky", "sky", "sky", "DOS", "English", "never"),
    ("lure", "lure", "lure", "DOS", "English", "never"),
    ("sword1", "sword1", "sword1", "Windows", "English", "never"),
    ("queen", "queen", "queen", "DOS", "English", "never"),
]
SV_RECENT = ["monkey-cd", "atlantis", "monkey2", "comi", "tentacle", "dig"]


def sv_packs():
    import zipfile
    return [zipfile.ZipFile(p) for p in sorted(glob.glob(os.path.join(SV_ICONS, "gui-icons-2*.dat")), reverse=True)]


def load_scummvm():
    """The favourites of SV_FAVOURITES with what ScummVM's icon packs say of
    them (games.xml, companies.xml, series.xml, engines.xml of the newest pack
    that has each), their icons the packs' own (512x512, the newest pack's)"""
    import io
    import xml.etree.ElementTree as ET
    packs = sv_packs()
    tables = {}
    for name, tag in (("games", "game"), ("companies", "company"), ("series", "serie"), ("engines", "engine")):
        for z in packs:
            if name + ".xml" in z.namelist():
                tables[name] = {e.get("id"): e for e in ET.fromstring(z.read(name + ".xml")).iter(tag)}
                break
    games = []
    for target, gid, eid, platform, language, when in SV_FAVOURITES:
        g = tables["games"][gid]
        icon = next(Image.open(io.BytesIO(z.read("icons/%s-%s.png" % (eid, gid)))).convert("RGBA")
                    for z in packs if "icons/%s-%s.png" % (eid, gid) in z.namelist())
        company = tables["companies"].get(g.get("company_id"))
        serie = tables["series"].get(g.get("series_id"))
        games.append({"id": target, "title": g.get("name"), "engine": "ScummVM", "system": "scummvm", "icon": icon,
                      "publisher": company.get("name") if company is not None else "",
                      "year": g.get("year"), "series": serie.get("name") if serie is not None else "",
                      "sv_engine": tables["engines"][eid].get("name"), "platform": platform, "language": language,
                      "when": when})
    return games


def sv_card(game):
    """A ScummVM game's card, as ScummVM writes it when the game is marked: its
    icon at SV_ICON in the middle of the screenshot's size, clear around it"""
    key = ("card", game["id"])
    if key not in _cache:
        card = Image.new("RGBA", SHOT, (0, 0, 0, 0))
        icon = to_linear_resize(game["icon"], (SV_ICON, SV_ICON))
        card.alpha_composite(icon, ((SHOT[0] - SV_ICON) // 2, (SHOT[1] - SV_ICON) // 2))
        _cache[key] = card
    return _cache[key]


def layer_card(game, xy, alpha=1.0):
    out = blank()
    out.alpha_composite(sv_card(game), (round(xy[0]), round(xy[1])))
    return fade(out, alpha)


TITLE_SIZES = (26, 22, 20)   # the roles big, mid and title: the program has its fonts at these sizes alone


def title_picture(F, title, box_w, box_h, size):
    """The title in the launcher's font where a port's logo is: two lines at
    most, each centred, at the largest of TITLE_SIZES from size down that fits
    the box; at the smallest the second line cut short"""
    for s in [t for t in TITLE_SIZES if t <= size]:
        f = F.sized(s)
        lines = wrap(title, f, box_w)
        lh = round(s * 1.12)
        if len(lines) <= 2 and len(lines) * lh <= box_h + 2 and max(f.getlength(t) for t in lines) <= box_w:
            break
    else:
        lines = [clip(t, f, box_w) for t in lines[:1]] + [clip(" ".join(lines[1:]), f, box_w)] if len(lines) > 1 \
            else [clip(lines[0], f, box_w)]
        if len(lines) * lh > box_h + 2:
            lines = [clip(title, f, box_w)]
    im = Image.new("RGBA", (box_w, len(lines) * lh), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for n, t in enumerate(lines):
        d.text((box_w / 2, n * lh + lh / 2), t, font=f, fill=INK + (255,), anchor="mm")
    return im.crop(im.getbbox())


def sv_rows(games, mode):
    if mode == "A-Z":
        return [("game", g["title"], g, "") for g in sorted(games, key=lambda g: g["title"].lower())]
    if mode == "Recent":
        by_id = {g["id"]: g for g in games}
        return [("game", by_id[i]["title"], by_id[i], by_id[i]["when"]) for i in SV_RECENT]
    key = {"Engine": "sv_engine", "Publisher": "publisher", "Year": "year"}[mode]
    rows = []
    for head in sorted({g[key] for g in games}, key=str.lower):
        rows.append(("head", head, None, ""))
        rows += [("game", g["title"], g, "") for g in sorted(games, key=lambda g: g["title"].lower()) if g[key] == head]
    return rows


class ScummMain(Main):
    """The System ScummVM's list: first the ScummVM port's own entry, "ScummVM
    launcher" (its logo and screenshot, as in Hybrid Ports before), in every
    sort mode; then the favourites, the title where a port's logo is, the card
    where its screenshot is"""
    title, chips, hints = "ScummVM", SV_CHIPS, SV_HINTS

    def __init__(self, F, badge_font, games, launcher, mode, cursor_id, family="ps"):
        self.F, self.badge_font, self.family, self.mode = F, badge_font, family, mode
        self.art, self.shift = "shot", False   # its artwork alone, whatever Game art says
        self.games = games
        self.rows = [("game", "ScummVM launcher", launcher, "")] + sv_rows(games, mode)
        self.count = "%d games" % len(games)
        self.strips = list_strips(F, self.rows)
        self.cursor = self.index(cursor_id)
        self.first = max(0, min(self.cursor - N_ROWS // 2, len(self.rows) - N_ROWS))

    def preview_logo(self, g, dx=0.0):
        if g.get("launcher"):
            return layer_preview_logo(g, dx)
        # the artwork alone (Danik, 2026-11-01): the title is on the list beside it
        return blank()

    def preview_shot(self, g, xy=None):
        if g.get("launcher"):
            return layer_shot(self.F, g, xy or preview_shot_xy())
        # centred in the pane, whatever Game art says: it has only the one picture
        return layer_card(g, xy or ((PREV_X[0] + PREV_X[1] - SHOT[0]) // 2, (LIST_Y[0] + LIST_Y[1] - SHOT[1]) // 2))


# Details of a ScummVM game: facts only (Danik, 2026-10-29), the page laid out
# as a port's (Danik picked it over two panels centred in the page): the card,
# the title where the logo is with the facts of the game under it, and in the
# bottom panel, where a port's description is, the facts of the copy (engine,
# platform, language, where its data is). A plays, B goes back; no Favourite:
# favourites are marked in ScummVM.

def sv_details_layers(F, badge_font, g, family="ps"):
    c = Canvas(ground=False)
    x0, y0, x1, y1 = D_INFO_PANEL
    bx, by, bw, bh = x0 + 12, y0 + 12, x1 - x0 - 24, 46
    im = title_picture(F, g["title"], bw, bh, 22)
    c.picture(im, (bx + (bw - im.width) // 2, by + bh - im.height))
    layer = c.layer()
    ImageDraw.Draw(layer).line((x0 + 14, by + bh + 8, x1 - 14, by + bh + 8), fill=EDGE + (90,))
    c.over(layer)
    top = [("Publisher", g["publisher"], INK), ("Year", g["year"], CYAN), ("Series", g["series"] or "none", INK),
           ("Last played", g["when"], INK)]
    lw = max(F.small.getlength(k) for k, v, ink in top) + 12
    for n, (k, v, ink) in enumerate(top):
        y = by + bh + 16 + n * 16
        c.text((x0 + 14, y + 1), k, F.small, DIM)
        c.text((x0 + 14 + lw, y), clip(v, F.chip, x1 - x0 - 28 - lw), F.chip, ink)
    info = c.im
    c = Canvas(ground=False)
    x0, y0, x1, y1 = D_DESC_PANEL
    copy = [("Engine", g["sv_engine"], INK), ("Platform", g["platform"], INK), ("Language", g["language"], INK),
            ("Game data", "games/dnk/ScummVM/%s/" % g["id"], INK)]
    half = (x1 - x0) // 2
    lw = max(F.small.getlength(k) for k, v, ink in copy) + 12
    for n, (k, v, ink) in enumerate(copy):
        cx, cy = x0 + 16 + (n // 2) * half, y0 + (y1 - y0 - 2 * 22) // 2 + (n % 2) * 22
        c.text((cx, cy + 1), k, F.small, DIM)
        c.text((cx + lw, cy), clip(v, F.chip, half - 32 - lw), F.chip, ink)
    return {"ground": layer_ground(), "logo": layer_logo(), "title": layer_title(F, "ScummVM"),
            "count": layer_count(F, "Details", "ScummVM"),
            "shot_panel": layer_panel(D_SHOT_PANEL), "shot": layer_card(g, D_SHOT),
            "info_panel": layer_panel(D_INFO_PANEL), "info": info,
            "desc_panel": layer_panel(D_DESC_PANEL), "desc": c.im,
            "hints": layer_hints(F, [("ok", "Play"), ("ports", "Repo"), ("back", "Back")], badge_font, family, second=DETAILS_SECOND),
            "badge": layer_badge(badge_font)}


def system_card(key):
    if ("system", key) not in _cache:
        _cache[("system", key)] = Image.open(os.path.join(SYSTEMS_DIR, key + ".png")).convert("RGBA")
    return _cache[("system", key)]


# The Systems screen as banners (Danik, 2026-10-31, from the options on
# https://claude.ai/artifact/PHYJuHtNcR3CBqTYAV4nnx; docs/dnklauncher.md, "Other
# cores"): wide banners, four in sight, Hybrid Ports and ScummVM first with their
# approved colours, then the favourites, then A to Z. A banner of a core is the
# scheme's panel with a strip of the system's colours at its left and its logo in
# the scheme's text colour. Those not installed, with the option on, come last
# in grey under a heading; the cursor goes on over them in a grey ring. The ring
# stays with its row, it does not float.
#
# Folded (Danik, 2026-11-02, on https://claude.ai/artifact/PzZoXFJAp9o3RYJSEDY1nF):
# only the banner under the cursor is BAN_H high, every other BAN_FOLD_H, its
# logo smaller and the maker alone on the right; seven in sight. The open banner
# stays the third in sight, the list moving under it, but at the list's ends.
# Moving, the banner left folds as the next opens; the folded contents fade out
# over the first half and the open ones in over the second.

BAN_H, BAN_GAP = 70, 8 - min(MARGIN // 2, 4)
BAN_X, BAN_W = M, W - 2 * M - 10        # the scroll bar's place at the right
BAN_TOP = MAIN[0] + 3
BAN_HEADING = 22         # the heading's height over the first row not installed
BAN_STRIP = 12           # the system's colours
BAN_RADIUS = 8
BAN_LOGO = (22, 240, 56)         # the logo's x and its largest size
BAN_RING = (2, 3)        # its gap to the banner, its width
RING_BREATHE = 1.5       # how far the ring grows and shrinks, in pixels either way
RING_BREATHE_HZ = 0.9    # (Danik, 2026-10-31: twice as fast as first)
RING_SWAP_HZ = 0.25      # the bar's two colours changing places along the ring
BAN_CHIPS = ["All", "Consoles", "Computers", "Hybrid", CHIP_STAR]
PICK_DX = 14             # L2 held: how far the picked banner slides out to the left
PICK_FIELDS = 10         # in how many fields
BAN_FOLD_H = 34          # a folded banner: half, less a pixel so that seven fit
BAN_FOLD_GAP = max(2, BAN_GAP // 2)   # between folded ones; the open one keeps BAN_GAP (the ring)
BAN_FOLD_LOGO = (150, 22)  # a folded banner's logo, its largest size
BAN_SLOT = 2             # the open banner's place: the third in sight
START_SYSTEMS_FIELDS = 52   # the start into Systems: eight banners rising, 2 fields apart

# what the mock-ups show of a player's MiSTer
MOCK_NOT_INSTALLED = ("crayxmp", "edsac", "pdp1", "sgiindy", "sun", "uknc", "vt52", "x68000")
MOCK_FAVOURITES = ("c64", "snes")
MOCK_COUNTS = {"hybrid": "43 games", "scummvm": "12 games"}


def read_systems():
    rows = []
    for line in open(os.path.join(SYSTEMS_DIR, "systems.txt"), encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        key, title, kind, maker, year, c1, c2, entries, desc = line.rstrip("\n").split("|")
        variants = []
        for e in entries.split(","):
            label, _, what = e.rpartition("=")
            variants.append((label, what.partition(">")[0]))
        rows.append(dict(key=key, title=title, kind=kind, maker=maker, year=year, c1=hex_rgb(c1), c2=hex_rgb(c2),
                         variants=variants, desc=desc, installed=key not in MOCK_NOT_INSTALLED,
                         fav=key in MOCK_FAVOURITES))
    return rows


def hex_rgb(h):
    h = h.lstrip("#") or "555566"
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mock_rows(chip="All", missing=False):
    """The rows of Systems. With a type chosen, its cores alone"""
    all_ = read_systems()
    rows = []
    if chip in ("All", "Hybrid"):   # Hybrid: the hybrid ports (the MIDI Player among them) and ScummVM
        rows = [dict(key=k, title=t, special=True, installed=True, fav=False, count=MOCK_COUNTS[k]) for k, t in SYSTEMS]
    of = [s for s in all_ if chip == "All" or s["kind"] + "s" == chip or (chip == CHIP_STAR and s["fav"])]
    title = lambda s: s["title"].lower()
    rows += sorted((s for s in of if s["installed"] and s["fav"]), key=title)
    rows += sorted((s for s in of if s["installed"] and not s["fav"]), key=title)
    if missing:
        rows += sorted((s for s in of if not s["installed"]), key=title)
    return rows


def openness(i, cur):
    """How far row i is open with the cursor at cur (a fraction while it moves)"""
    return max(0.0, 1.0 - abs(i - cur))


def row_h(i, cur):
    return BAN_FOLD_H + (BAN_H - BAN_FOLD_H) * openness(i, cur)


def row_gap(i, cur):
    """The gap under row i"""
    return BAN_FOLD_GAP + (BAN_GAP - BAN_FOLD_GAP) * max(openness(i, cur), openness(i + 1, cur))


def row_y(rows, i, cur):
    """Row i's top in the list, from the list's top, with the cursor at cur"""
    y = sum(row_h(j, cur) + row_gap(j, cur) for j in range(i))
    if not rows[i]["installed"]:
        y += BAN_HEADING
    return y


def list_height(rows, cur):
    return row_y(rows, len(rows) - 1, cur) + row_h(len(rows) - 1, cur) if rows else 0


def list_scroll(rows, cursor, at=0):
    """The list's offset with the cursor on row `cursor`: its banner the third
    in sight, but at the list's two ends"""
    view = MAIN[1] - BAN_TOP
    above = min(cursor, BAN_SLOT)
    at = row_y(rows, cursor, cursor) - (above * (BAN_FOLD_H + BAN_FOLD_GAP) + (BAN_GAP - BAN_FOLD_GAP if above else 0))
    return max(0, min(at, max(0, list_height(rows, cursor) - view)))


def round_mask(w, h, r):
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1), r, fill=255)
    return mask


def gradient(w, h, c1, c2, vertical=False):
    im = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(im)
    n = (h if vertical else w) - 1 or 1
    for i in range(n + 1):
        col = tuple(round(a + (b - a) * i / n) for a, b in zip(c1, c2)) + (255,)
        d.line((0, i, w, i) if vertical else (i, 0, i, h), fill=col)
    return im


def system_logo(F, s):
    """A core's logo as a mask"""
    path = os.path.join(SYSTEMS_DIR, "logos", s["key"] + ".png")
    if os.path.exists(path):
        return Image.open(path).convert("L")
    font = F.sized(26)
    im = Image.new("L", (round(font.getlength(s["title"])) + 4, 40), 0)
    ImageDraw.Draw(im).text((2, 20), s["title"], font=font, fill=255, anchor="lm")
    return im.crop(im.getbbox())


# Options' System logos (Danik, 2026-10-31): one colour unless picked, else the theme's
# logos in their own colours, where there is one: those a system has none of stay in
# one colour, as do the grey rows and the card of Details (red on a red card goes)
LOGO_COLOUR = [False]


def system_logo_colour(s):
    """A core's logo in its colours, and the mask of what the text colour draws, or None"""
    base = os.path.join(SYSTEMS_DIR, "logos_colour", s["key"])
    if not os.path.exists(base + ".png"):
        return None, None
    ink = Image.open(base + "_ink.png").convert("L") if os.path.exists(base + "_ink.png") else None
    return Image.open(base + ".png").convert("RGBA"), ink


def inked(mask, ink, alpha=1.0):
    im = Image.new("RGBA", mask.size, ink + (0,))
    im.putalpha(mask.point(lambda v: round(v * alpha)))
    return im


def fit(im, w, h):
    """im made smaller to fit in w x h, never larger"""
    r = min(w / im.width, h / im.height, 1.0)
    if r < 1:
        im = im.resize((max(1, round(im.width * r)), max(1, round(im.height * r))), Image.LANCZOS)
    return im


def special_art(key, folded=False):
    """The picture on Hybrid Ports' or ScummVM's banner, scaled, and its place"""
    if folded:   # the open one's made smaller (make_assets.py bakes it)
        art, at = special_art(key)
        h = BAN_FOLD_H
        r = (h - (8 if key == "hybrid" else 12)) / art.height
        art = art.resize((max(1, round(art.width * r)), max(1, round(art.height * r))), Image.LANCZOS)
        return art, (at[0], (h - art.height) // 2)
    h = BAN_H
    if key == "hybrid":
        cat = system_card(key).crop((10, 6, 246, 142))
        r = (h - 8) / cat.height
        return cat.resize((round(cat.width * r), round(cat.height * r)), Image.LANCZOS), (10, 4)
    logo = Image.open(os.path.join(SYSTEMS_DIR, "scummvm_logo.png")).convert("RGBA")
    logo = logo.crop(logo.getbbox())
    r = min((h - 12) / logo.height, 150 / logo.width)
    logo = logo.resize((round(logo.width * r), round(logo.height * r)), Image.LANCZOS)
    return logo, (14, (h - logo.height) // 2)


def special_banner(F, s, folded=False, bare=False):
    """Hybrid Ports' or ScummVM's: the approved card's colours and what it shows.
    bare: what it shows alone, without its ground"""
    w, h = BAN_W, BAN_FOLD_H if folded else BAN_H
    art, at = special_art(s["key"], folded)
    if s["key"] == "hybrid":
        ink = (0x1E, 0x1A, 0x2C)
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0) if bare else system_card(s["key"]).getpixel((3, 3))[:3] + (255,))
        im.alpha_composite(art, at)
        d = ImageDraw.Draw(im)
        d.text((18 + art.width, h // 2), s["title"], font=F.sized(20 if folded else 26), fill=ink + (255,), anchor="lm")
    else:
        ink = INK
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0)) if bare else gradient(w, h, BG_TOP, BG_BOTTOM)
        im.alpha_composite(art, at)
        if not bare:
            ImageDraw.Draw(im).rounded_rectangle((0, 0, w - 1, h - 1), BAN_RADIUS, outline=EDGE + (200,))
        d = ImageDraw.Draw(im)
    d.text((w - 14, h // 2), s["count"], font=F.chip, fill=ink + (255,), anchor="rm")
    if not bare:
        im.putalpha(round_mask(w, h, BAN_RADIUS))
    return im


def banner_panel(s, h):
    """A banner's ground h high, without what it shows"""
    w = BAN_W
    if s.get("special"):
        if s["key"] == "hybrid":
            im = Image.new("RGBA", (w, h), system_card(s["key"]).getpixel((3, 3))[:3] + (255,))
        else:
            im = gradient(w, h, BG_TOP, BG_BOTTOM)
            ImageDraw.Draw(im).rounded_rectangle((0, 0, w - 1, h - 1), BAN_RADIUS, outline=EDGE + (200,))
        im.putalpha(round_mask(w, h, BAN_RADIUS))
        return im
    grey = not s["installed"]
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), BAN_RADIUS, fill=PANEL + (120 if grey else PANEL_ALPHA,),
                        outline=EDGE + (70 if grey else 150,))
    d.line((BAN_RADIUS, 1, w - BAN_RADIUS, 1), fill=(255, 255, 255, 16 if grey else 50))
    if not grey:
        strip = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        strip.paste(gradient(BAN_STRIP, h, s["c1"], s["c2"], vertical=True), (1, 0))
        mask = Image.new("L", (w, h), 0)
        ImageDraw.Draw(mask).rounded_rectangle((1, 1, w - 2, h - 2), BAN_RADIUS - 1, fill=255)
        ImageDraw.Draw(mask).rectangle((BAN_STRIP + 1, 0, w, h), fill=0)
        strip.putalpha(mask)
        im.alpha_composite(strip)
    return im


def banner(F, s, h=BAN_H):
    """A banner h high: open (BAN_H), folded (BAN_FOLD_H), or in between, its
    ground at that height and the folded and open faces fading out and in"""
    h = max(BAN_FOLD_H, min(BAN_H, round(h)))
    k = ("banner", s["key"], PANEL, INK, id(F), s["installed"], s["fav"], LOGO_COLOUR[0], s.get("hidden"), s.get("line2"), h)
    if k in _cache:
        return _cache[k]
    if h in (BAN_H, BAN_FOLD_H):
        im = banner_face(F, s, h == BAN_FOLD_H)
    else:
        im = banner_panel(s, h)
        t = (h - BAN_FOLD_H) / (BAN_H - BAN_FOLD_H)
        for folded, a in ((True, max(0.0, 1 - 2 * t)), (False, max(0.0, 2 * t - 1))):
            if a <= 0:
                continue
            face = banner_face(F, s, folded, bare=True)
            face = fade(face, a) if a < 1 else face
            y = (h - face.height) // 2
            im.alpha_composite(face.crop((0, max(0, -y), BAN_W, max(0, -y) + min(h, face.height))), (0, max(0, y)))
        im.putalpha(ImageChops.multiply(im.getchannel("A"), round_mask(BAN_W, h, BAN_RADIUS)))
    if s.get("hidden"):   # shown by Options' Show hidden: dimmed and marked
        im = im.copy()
        im.putalpha(im.getchannel("A").point(lambda v: round(v * 0.45)))
        d = ImageDraw.Draw(im)
        tw = F.small.getlength("hidden")
        d.text((BAN_W // 2 - tw / 2, h // 2), "hidden", font=F.small, fill=DIM + (255,), anchor="lm")
    _cache[k] = im
    return im


def banner_face(F, s, folded=False, bare=False):
    """An open or folded banner (bare: what it shows alone, without its ground)"""
    w, h = BAN_W, BAN_FOLD_H if folded else BAN_H
    if s.get("special"):
        return special_banner(F, s, folded, bare)
    grey = not s["installed"]
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if not bare:
        d.rounded_rectangle((0, 0, w - 1, h - 1), BAN_RADIUS, fill=PANEL + (120 if grey else PANEL_ALPHA,),
                            outline=EDGE + (70 if grey else 150,))
        d.line((BAN_RADIUS, 1, w - BAN_RADIUS, 1), fill=(255, 255, 255, 16 if grey else 50))
    ink, dim = INK, DIM
    x = 14
    if grey:
        ink = dim = tuple(round(a * 0.62 + b * 0.38) for a, b in zip(DIM, PANEL))
    else:
        x = BAN_LOGO[0]
        if not bare:
            strip = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            strip.paste(gradient(BAN_STRIP, h, s["c1"], s["c2"], vertical=True), (1, 0))
            mask = Image.new("L", (w, h), 0)
            ImageDraw.Draw(mask).rounded_rectangle((1, 1, w - 2, h - 2), BAN_RADIUS - 1, fill=255)
            ImageDraw.Draw(mask).rectangle((BAN_STRIP + 1, 0, w, h), fill=0)
            strip.putalpha(mask)
            im.alpha_composite(strip)
    lw, lh = BAN_FOLD_LOGO if folded else BAN_LOGO[1:]
    pic, part = system_logo_colour(s) if LOGO_COLOUR[0] and not grey else (None, None)
    if pic and folded:   # (make_assets.py bakes these)
        pic = fit(pic, lw, lh)
        part = part.resize(pic.size, Image.LANCZOS) if part else None
    if pic:
        if part:
            im.alpha_composite(inked(part, ink), (x, (h - pic.height) // 2))
        im.alpha_composite(pic, (x, (h - pic.height) // 2))
    else:
        mask = fit(system_logo(F, s), lw, lh)
        im.alpha_composite(inked(mask, ink, 0.8 if grey else 1.0), (x, (h - mask.height) // 2))
    c = Canvas(ground=False)
    c.im = im
    d = ImageDraw.Draw(im)
    right = w - 14
    if grey:
        d.text((right, h // 2), "Not installed", font=F.small if folded else F.chip, fill=ink + (255,), anchor="rm")
    elif folded:
        d.text((right, h // 2), s["maker"], font=F.small, fill=dim + (255,), anchor="rm")
        if s["fav"]:
            Canvas.star(c, right - F.small.getlength(s["maker"]) - 12, h // 2, 5, GOLD)
    else:
        d.text((right, h // 2 - 9), s["maker"], font=F.chip, fill=ink + (255,), anchor="rm")
        d.text((right, h // 2 + 9), s.get("line2") or "%s  %s" % (s["year"], s["kind"]), font=F.small, fill=dim + (255,),
               anchor="rm")
        if s["fav"]:
            Canvas.star(c, right - F.chip.getlength(s["maker"]) - 12, h // 2 - 9, 6, GOLD)
    return im


def shadow_of(w, h):
    k = ("banner_shadow", w, h)
    if k not in _cache:
        pad = 10
        im = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
        ImageDraw.Draw(im).rounded_rectangle((pad + 1, pad + 3, pad + w, pad + h + 2), BAN_RADIUS, fill=(20, 0, 30, 110))
        _cache[k] = im.filter(ImageFilter.GaussianBlur(4))
    return _cache[k]


def band(layer):
    """What of the banners lies within the band of the list"""
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rectangle((0, MAIN[0] - 6, W, MAIN[1] + 5), fill=255)
    layer.putalpha(ImageChops.multiply(layer.getchannel("A"), mask))
    return layer


def layer_banner(F, s, y, h=BAN_H):
    out = blank()
    y = round(y)
    if s["installed"]:
        out.alpha_composite(shadow_of(BAN_W, max(BAN_FOLD_H, min(BAN_H, round(h)))), (BAN_X - 10, max(0, y - 10)))
    out.alpha_composite(banner(F, s, h), (BAN_X, y))
    return band(out)


def ring_colours(x, w, field, swap):
    """The bar's colours at column x of a ring w wide: pink to peach as the bar;
    with swap the two change places along it and back"""
    t = x / (w - 1)
    if swap:
        phase = math.sin(field / 59.64 * math.pi * 2 * RING_SWAP_HZ)   # -1..1: which way the gradient runs
        t = 0.5 + (t - 0.5) * phase
    return tuple(round(a + (b - a) * t) for a, b in zip(PINK, PEACH))


def layer_ring_at(y, field=0, style="both", grey=False):
    """The cursor. style: still, breathe (it grows and shrinks a little), swap
    (its colours change places), both. Drawn 4 times larger, so that it grows
    by less than a pixel at a time"""
    gap, width = BAN_RING
    if style in ("breathe", "both") and not grey:
        gap += RING_BREATHE * (0.5 + 0.5 * math.sin(field / 59.64 * math.pi * 2 * RING_BREATHE_HZ)) * 2 - RING_BREATHE + 0
    o = gap + width
    big = 4
    pad = 9
    bw, bh = BAN_W + 2 * pad, BAN_H + 2 * pad
    mask = Image.new("L", (bw * big, bh * big), 0)
    d = ImageDraw.Draw(mask)
    d.rounded_rectangle(((pad - o) * big, (pad - o) * big, (pad + BAN_W + o) * big - 1, (pad + BAN_H + o) * big - 1),
                        (BAN_RADIUS + o) * big, fill=255)
    d.rounded_rectangle(((pad - gap) * big, (pad - gap) * big, (pad + BAN_W + gap) * big - 1,
                         (pad + BAN_H + gap) * big - 1), (BAN_RADIUS + max(gap, 0)) * big, fill=0)
    mask = mask.resize((bw, bh), Image.BOX)
    ring = Image.new("RGBA", (bw, bh), DIM + (0,))
    if not grey:
        rd = ImageDraw.Draw(ring)
        for x in range(bw):
            rd.line((x, 0, x, bh), fill=ring_colours(x, bw, field, style in ("swap", "both")) + (0,))
    ring.putalpha(mask.point(lambda v: round(v * 0.78)) if grey else mask)
    out = blank()
    out.alpha_composite(ring, (BAN_X - pad, max(0, round(y) - pad)))
    return band(out)


def layer_list_bar(rows, at, cur):
    view = MAIN[1] - BAN_TOP
    total = list_height(rows, cur)
    out = blank()
    if total <= view:
        return out
    d = ImageDraw.Draw(out)
    x, y0, y1 = W - M - 4, MAIN[0], MAIN[1]
    d.rounded_rectangle((x, y0, x + 3, y1 - 1), 2, fill=EDGE + (60,))
    h = y1 - y0
    knob = max(16, h * view // total)
    k0 = y0 + (h - knob) * at / (total - view)
    d.rounded_rectangle((x, k0, x + 3, k0 + knob - 1), 2, fill=EDGE + (230,))
    return out


def sys_second(s):
    if s["installed"]:
        return [("page", "Page"), ("l2", "Hold: Move"), ("sort", "Hold: Shift"), ("options", "Options")]
    return [("page", "Page"), ("sort", "Hold: Shift"), ("options", "Options")]


def sys_hints(s):
    if s.get("special"):
        return [("ok", "Open"), ("ports", "Repo"), ("back", "Exit")]
    if not s["installed"]:
        return [("ports", "Repo"), ("back", "Exit")]
    return [("ok", "Details"), ("fav", "Favourite"), ("ports", "Repo"), ("back", "Exit")]


PICK_HINTS = [("l2", "Up / Down: Move, Release: Place")]


def systems_layers(F, badge_font, rows, cursor, at, chip="All", family="ps", field=0, style="both", pick=None, shift=False,
                   open_at=None, ring_y=None):
    """Systems with the cursor on row `cursor` (a fraction while it moves), the
    list at offset `at`, at a field of the ring. pick: L2 held, the row under
    the cursor picked: (how far it is out to the left, {row: its offset down
    from its place, while it makes way}, False once L2 is let go). open_at:
    the row that is open, if not the cursor's (the picked one while it moves);
    ring_y: the cursor's top in the list, if not its row's"""
    if open_at is None:
        open_at = cursor
    c = round(cursor)
    L = {"ground": layer_ground(), "logo": layer_logo(), "title": layer_title(F, "Systems"),
         "count": layer_count(F, "%d systems" % sum(s["installed"] for s in rows), "Systems"),
         "chips": layer_chips(F, family, chip_rects(F, BAN_CHIPS)[chip], BAN_CHIPS),
         "badge": layer_badge(badge_font)}
    if not rows:
        L["hints"] = layer_hints(F, sys_hints(dict(installed=False)), badge_font, family)
        L["banners"], L["cursor"], L["bar"] = blank(), blank(), blank()
        if chip == CHIP_STAR:
            none = Canvas(ground=False)
            none.text(((M + W - M) / 2, (MAIN[0] + MAIN[1]) / 2), "No favourites yet", F.chip, DIM, anchor="mm")
            L["banners"] = none.im
        return L
    # Hybrid Ports and ScummVM cannot be hidden: nothing else would lead to their games
    L["hints"] = (layer_hints(F, [] if rows[c].get("special") else shift_hints(rows[c].get("hidden")), badge_font, family, second=SHIFT_SECOND) if shift else
                  layer_hints(F, PICK_HINTS, badge_font, family) if pick and (len(pick) < 3 or pick[2]) else
                  layer_hints(F, sys_hints(rows[c]), badge_font, family, second=sys_second(rows[c])))
    others = blank()
    for n, s in enumerate(rows):
        if pick and n == c:
            continue
        y = BAN_TOP + row_y(rows, n, open_at) - at
        if pick:
            y += pick[1].get(n, 0)
        if y + BAN_H < MAIN[0] - 8 or y > MAIN[1] + 8:
            continue
        banner_layer = layer_banner(F, s, y, row_h(n, open_at))
        L["ban%d" % n] = banner_layer
        others.alpha_composite(banner_layer)
        if not s["installed"] and (n == 0 or rows[n - 1]["installed"]):
            head = blank()
            ImageDraw.Draw(head).text((BAN_X + 4, y - 8), "Not installed: %d cores" % sum(not r["installed"] for r in rows),
                                      font=F.chip, fill=INK + (255,), anchor="lb")
            others.alpha_composite(band(head))
    L["banners"] = others
    a, b = int(math.floor(cursor)), min(int(math.floor(cursor)) + 1, len(rows) - 1)
    y = (lerp(row_y(rows, a, open_at), row_y(rows, b, open_at), cursor - a) if ring_y is None else ring_y) + BAN_TOP - at
    L["cursor"] = layer_ring_at(y, field, style, grey=not rows[c]["installed"])
    if pick:
        dx = -round(pick[0])
        others.alpha_composite(place(layer_banner(F, rows[c], y), dx))
        L["cursor"] = place(L["cursor"], dx)
    L["bar"] = layer_list_bar(rows, at, open_at)
    return L


SYS_ORDER = ["ground", "logo", "title", "count", "chips", "banners", "cursor", "bar", "hints", "badge"]


def systems_screen(F, badge_font, rows, cursor, at=None, chip="All", family="ps", field=0, style="both", pick=None, shift=False,
                   open_at=None, ring_y=None):
    if at is None:
        at = list_scroll(rows, cursor, 0) if rows else 0
    return compose(systems_layers(F, badge_font, rows, cursor, at, chip, family, field, style, pick, shift, open_at, ring_y),
                   SYS_ORDER).convert("RGB")


def anim_ring(F, badge_font, rows, style):
    """The cursor still on a row for four seconds"""
    return [systems_screen(F, badge_font, rows, 3, field=f, style=style) for f in range(0, 240, 2)]


def anim_systems_cursor(F, badge_font, rows):
    """The cursor down, held: a row every 12 fields, gliding in 7 as the list's
    bar does, the banner it leaves folding as the next opens, the list moving
    under it; then up"""
    frames, field = [], 0
    cur, at = 0, 0.0
    for step in [1] * 9 + [-1] * 6:
        nxt = cur + step
        at2 = list_scroll(rows, nxt, round(at))
        for f in range(12):
            e = ease_out(span(f, 0, 7))
            frames.append(systems_screen(F, badge_font, rows, lerp(cur, nxt, e), lerp(at, at2, e), field=field))
            field += 1
        cur, at = nxt, at2
    return frames


def anim_system_pick(F, badge_font, rows, start):
    """L2 held (Danik, 2026-10-31): the banner slides out to the left, Down and
    Up move it, the row it passes making way, and let go it slides back"""
    rows = list(rows)
    frames, field = [], 0
    cur, at = start, list_scroll(rows, start, 0)
    for f in range(PICK_FIELDS + 6):
        out = PICK_DX * ease_out(span(f, 0, PICK_FIELDS))
        frames.append(systems_screen(F, badge_font, rows, cur, at, field=field, pick=(out, {})))
        field += 1
    for step in (1, 1, 1, -1):
        nxt = cur + step
        was = list(rows)
        rows[cur], rows[nxt] = rows[nxt], rows[cur]
        at2 = list_scroll(rows, nxt, at)
        # the picked banner stays open, the row it passes folded: each glides
        # from its place before to its place now
        y0, y1 = row_y(was, cur, cur), row_y(rows, nxt, nxt)
        for f in range(14):
            e = ease_out(span(f, 0, 7))
            make_way = (row_y(was, nxt, cur) - row_y(rows, cur, nxt)) * (1 - e)
            frames.append(systems_screen(F, badge_font, rows, nxt, lerp(at, at2, e), field=field,
                                         pick=(PICK_DX, {cur: make_way}), open_at=nxt, ring_y=lerp(y0, y1, e)))
            field += 1
        cur, at = nxt, at2
    for f in range(PICK_FIELDS + 6):
        out = PICK_DX * (1 - ease_out(span(f, 0, PICK_FIELDS)))
        frames.append(systems_screen(F, badge_font, rows, cur, at, field=field, pick=(out, {}, False) if out > 0 else None))
        field += 1
    return frames


def anim_chip(F, badge_font, rows_a, rows_b, a, b):
    """Select: the pill slides as in the game list, the rows give way to the
    others, which come up"""
    frames = []
    rects = chip_rects(F, BAN_CHIPS)
    La = systems_layers(F, badge_font, rows_a, 2, 0, a)
    Lb = systems_layers(F, badge_font, rows_b, 1, 0, b)
    for f in range(20):
        fr = compose({k: La[k] for k in ("ground", "logo", "title", "hints", "badge")},
                     ["ground", "logo", "title", "hints", "badge"]).convert("RGBA")
        e = ease_out(span(f, 0, 8))
        x0, w0 = rects[a]
        x1, w1 = rects[b]
        fr.alpha_composite(layer_chips(F, "ps", (lerp(x0, x1, e), lerp(w0, w1, e)), BAN_CHIPS))
        fr.alpha_composite(place(La["count"], alpha=1 - span(f, 0, 6)))
        fr.alpha_composite(place(Lb["count"], alpha=span(f, 6, 12)))
        out = ease_in_out(span(f, 0, 9))
        fr.alpha_composite(place(La["banners"], 0, 10 * out, 1 - out))
        fr.alpha_composite(place(La["cursor"], alpha=1 - span(f, 0, 5)))
        inn = ease_out(span(f, 6, 18))
        fr.alpha_composite(place(Lb["banners"], 0, 14 * (1 - inn), inn))
        fr.alpha_composite(place(Lb["cursor"], 0, 14 * (1 - inn), inn))
        fr.alpha_composite(place(Lb["bar"], alpha=inn))
        frames.append(fr.convert("RGB"))
    return frames


def anim_system_open(F, badge_font, rows, m, n, back=False):
    """A on Hybrid Ports or ScummVM: the other banners sink and fade, the card
    comes in where the preview's screenshot goes and crosses over into the game
    under the list's cursor, the banner slides into the list coming in from the
    left, the sort modes change; B on the list is the same backwards"""
    S = systems_layers(F, badge_font, rows, n, 0)
    L = m.frame_layers()
    g = m.game()
    base = compose({k: L[k] for k in ("ground", "logo", "badge")}, ["logo", "badge"])
    key = rows[n]["key"]
    y = BAN_TOP + row_y(rows, n, n)
    others = blank()
    for i in range(len(rows)):
        if i != n and "ban%d" % i in S:
            others.alpha_composite(S["ban%d" % i])
    c1 = preview_shot_xy()
    frames = []
    total = 24
    for f in range(total):
        k = total - 1 - f if back else f
        fr = base.copy()
        fr.alpha_composite(place(S["title"], alpha=1 - span(k, 0, 6)))
        fr.alpha_composite(place(L["title"], alpha=span(k, 6, 12)))
        fr.alpha_composite(place(S["count"], alpha=1 - span(k, 0, 6)))
        fr.alpha_composite(place(L["count"], 8 * (1 - ease_out(span(k, 8, 14))), 0, span(k, 8, 14)))
        fr.alpha_composite(place(S["chips"], 0, -8 * ease_in_out(span(k, 0, 7)), 1 - span(k, 0, 7)))
        q = ease_out(span(k, 9, 16))
        fr.alpha_composite(place(L["chips"], 0, -8 * (1 - q), q))
        p = ease_in_out(span(k, 0, 8))
        fr.alpha_composite(place(others, 0, 12 * p, 1 - p))
        fr.alpha_composite(place(S["bar"], alpha=1 - p))
        fr.alpha_composite(place(S["cursor"], alpha=1 - span(k, 0, 5)))
        t = ease_in_out(span(k, 1, 11))
        fr.alpha_composite(place(S["ban%d" % n], -40 * t, 0, 1 - t))
        fr.alpha_composite(place(L["prev_panel"], alpha=ease_out(span(k, 6, 14))))
        r = ease_out(span(k, 10, 20))
        for name in ("list_panel", "rows"):
            fr.alpha_composite(place(L[name], -28 * (1 - r), 0, r))
        cross = span(k, 13, 19)
        come = ease_out(span(k, 4, 13))
        card = blank()
        card.alpha_composite(system_card(key), (round(c1[0]), round(c1[1] + 12 * (1 - come))))
        fr.alpha_composite(place(card, alpha=come * (1 - cross)))
        if cross > 0:
            fr.alpha_composite(place(m.preview_shot(g, c1), alpha=cross))
        s = ease_out(span(k, 16, 23))
        if s > 0:
            fr.alpha_composite(place(m.preview_logo(g, 10 * (1 - s)), alpha=s))
        fr.alpha_composite(place(S["hints"], alpha=1 - span(k, 0, 6)))
        fr.alpha_composite(place(L["hints"], alpha=span(k, 12, 19)))
        frames.append(fr.convert("RGB"))
    return frames


def anim_start_systems(F, badge_font, rows):
    """Start in: Systems. From the core's own screen as anim_start(), the
    banners rising one after the other, the ring round the one it is on"""
    S = systems_layers(F, badge_font, rows, 0, 0)
    flat = Image.new("RGBA", (W, H), WALL_BG + (255,))
    groups = [(["title", "count"], 18, 32, 16, 0), (["chips"], 20, 34, 0, -8)]
    shown = [n for n in range(len(rows)) if "ban%d" % n in S]   # those in sight, one after the other
    groups += [(["ban%d" % n], 20 + 2 * k, 36 + 2 * k, 0, 18) for k, n in enumerate(shown)]
    groups += [(["cursor"], 30, 42, 0, 0), (["bar"], 30, 42, 0, 0), (["hints"], 28, 44, 0, 10)]
    frames = []
    for f in range(START_SYSTEMS_FIELDS):
        fr = flat.copy()
        fr.alpha_composite(place(S["ground"], alpha=ease_in_out(span(f, 4, 28))))
        t = ease_in_out(span(f, 4, 30))
        fr.alpha_composite(layer_logo(lerp(1.0, 0.5, t), (lerp(WALL_LOGO[0], M - 4, t), lerp(WALL_LOGO[1], HEAD[0], t))))
        fr.alpha_composite(layer_badge(badge_font, t))
        for names, a, b, dx, dy in groups:
            p = ease_out(span(f, a, b))
            if p > 0:
                for nm in names:
                    fr.alpha_composite(place(S[nm], dx * (1 - p), dy * (1 - p), p))
        frames.append(fr.convert("RGB"))
    return frames


# Details of a core (Danik, 2026-10-31): laid out as a game's, its logo on the
# system's colours where the screenshot goes. A says Start. A system with several
# cores (Game Boy and its two-player core, the Macs) has a row Version in its
# facts, left and right changing it; only the ones installed are offered.

def core_card(F, s):
    k = ("core_card", s["key"], id(F))
    if k not in _cache:
        im = gradient(SHOT[0], SHOT[1], s["c1"], s["c2"])
        mask = system_logo(F, s)
        r = min((SHOT[0] - 16) / mask.width, 120 / mask.height, 1.0)
        if r < 1:
            mask = mask.resize((max(1, round(mask.width * r)), max(1, round(mask.height * r))), Image.LANCZOS)
        im.alpha_composite(inked(mask, (255, 255, 255)), ((SHOT[0] - mask.width) // 2, (SHOT[1] - mask.height) // 2))
        im.putalpha(round_mask(SHOT[0], SHOT[1], 6))
        _cache[k] = im
    return _cache[k]


CORE_HINTS = [("ok", "Start"), ("fav", "Favourite"), ("ports", "Repo"), ("back", "Back")]


def core_facts(s, version):
    label, core = s["variants"][version]
    facts = [("Maker", s["maker"], INK), ("Year", s["year"], CYAN), ("Type", s["kind"], INK)]
    if label:
        facts.append(("Version", label, None))
    facts += [("Core", "%s_20260823" % core, CYAN), ("Last played", "3 days ago", INK)]
    return facts


def core_details_layers(F, badge_font, s, version=0, family="ps"):
    title = s["title"]
    L = {"ground": layer_ground(), "logo": layer_logo(), "title": layer_title(F, title),
         "count": layer_count(F, "Details", title), "badge": layer_badge(badge_font),
         "hints": layer_hints(F, CORE_HINTS, badge_font, family, second=DETAILS_SECOND),
         "shot_panel": layer_panel(D_SHOT_PANEL), "info_panel": layer_panel(D_INFO_PANEL),
         "desc_panel": layer_panel(D_DESC_PANEL)}
    pic = blank()
    pic.alpha_composite(core_card(F, s), D_SHOT)
    L["shot"] = pic
    x0, y0, x1, y1 = D_INFO_PANEL
    pw = x1 - x0
    c = Canvas(ground=False)
    t = clip(title, F.title, pw - 28 - (18 if s["fav"] else 0))
    c.text((x0 + 14, y0 + 14), t, F.title, INK)
    if s["fav"]:
        c.star(x0 + 24 + F.title.getlength(t), y0 + 26, 7, GOLD)
    layer = c.layer()
    ImageDraw.Draw(layer).line((x0 + 14, y0 + 45, x1 - 14, y0 + 45), fill=EDGE + (90,))
    c.over(layer)
    facts = core_facts(s, version)
    lw = max(F.small.getlength(k) for k, v, ink in facts) + 12
    for n, (k, v, ink) in enumerate(facts):
        y = y0 + 56 + n * 22
        c.text((x0 + 14, y + 1), k, F.small, DIM)
        if ink is None:     # the choice of version: its arrows and the value between them
            ax = x0 + 14 + lw
            layer = c.layer()
            d = ImageDraw.Draw(layer)
            d.polygon(((ax + 6, y + 3), (ax, y + 8), (ax + 6, y + 13)), fill=PINK + (255,))
            room = pw - 28 - lw - 30
            v = clip(v, F.chip, room)
            c.over(layer)
            c.text((ax + 12, y), v, F.chip, INK)
            bx = ax + 12 + F.chip.getlength(v) + 6
            layer = c.layer()
            ImageDraw.Draw(layer).polygon(((bx, y + 3), (bx + 6, y + 8), (bx, y + 13)), fill=PINK + (255,))
            c.over(layer)
        else:
            c.text((x0 + 14 + lw, y), clip(v, F.chip, pw - 28 - lw), F.chip, ink)
    L["info"] = c.im
    c = Canvas(ground=False)
    x0, y0, x1, y1 = D_DESC_PANEL
    lines = wrap(s["desc"], F.desc, x1 - x0 - 32)
    top = y0 + (y1 - y0 - len(lines) * 17) // 2
    for n, line in enumerate(lines):
        c.text((x0 + 16, top + n * 17), line, F.desc, INK)
    L["desc"] = c.im
    return L


def core_details_screen(F, badge_font, s, version=0):
    return compose(core_details_layers(F, badge_font, s, version), D_ORDER).convert("RGB")


def anim_core_details(F, badge_font, rows, n, back=False):
    """A on a core's banner: the other banners sink and fade, the logo leaves its
    banner for the card, which comes in round it, the panels rise; B the same
    backwards"""
    s = rows[n]
    at = list_scroll(rows, n, 0)
    S = systems_layers(F, badge_font, rows, n, at)
    D = core_details_layers(F, badge_font, s)
    base = compose({k: S[k] for k in ("ground", "logo", "badge")}, ["logo", "badge"])
    y = BAN_TOP + row_y(rows, n, n) - at
    others = blank()
    for i in range(len(rows)):
        if i != n and "ban%d" % i in S:
            others.alpha_composite(S["ban%d" % i])
    frames = []
    total = 24
    panel_keys = ("shot_panel", "info_panel", "desc_panel")
    for f in range(total):
        k = total - 1 - f if back else f
        fr = base.copy()
        fr.alpha_composite(place(S["title"], alpha=1 - span(k, 0, 6)))
        fr.alpha_composite(place(D["title"], alpha=span(k, 6, 12)))
        fr.alpha_composite(place(S["count"], alpha=1 - span(k, 0, 6)))
        fr.alpha_composite(place(D["count"], alpha=span(k, 8, 14)))
        fr.alpha_composite(place(S["chips"], 0, -8 * ease_in_out(span(k, 0, 7)), 1 - span(k, 0, 7)))
        p = ease_in_out(span(k, 0, 8))
        fr.alpha_composite(place(others, 0, 12 * p, 1 - p))
        fr.alpha_composite(place(S["bar"], alpha=1 - p))
        fr.alpha_composite(place(S["cursor"], alpha=1 - span(k, 0, 5)))
        fr.alpha_composite(place(S["ban%d" % n], 0, 0, 1 - ease_in_out(span(k, 1, 9))))
        r = ease_out(span(k, 6, 18))
        for name in panel_keys:
            fr.alpha_composite(place(D[name], 0, 16 * (1 - r), r))
        q = ease_in_out(span(k, 4, 16))
        fr.alpha_composite(place(D["shot"], 0, lerp(y - D_SHOT[1] - 61, 0, q), q))
        w = ease_out(span(k, 12, 22))
        fr.alpha_composite(place(D["info"], 12 * (1 - w), 0, w))
        fr.alpha_composite(place(D["desc"], 0, 8 * (1 - w), w))
        fr.alpha_composite(place(S["hints"], alpha=1 - span(k, 0, 6)))
        fr.alpha_composite(place(D["hints"], alpha=span(k, 12, 19)))
        frames.append(fr.convert("RGB"))
    return frames


def anim_core_start(F, badge_font, s):
    """Start: as Play (anim_play), the card the screenshot"""
    full = compose(core_details_layers(F, badge_font, s), D_ORDER)
    src = Image.new("RGB", SHOT, (0, 0, 0))
    card = core_card(F, s)
    src.paste(card.convert("RGB"), (0, 0))
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
            fr.paste(src.resize((round(w), round(h)), Image.NEAREST), (round(lerp(sx, 0, t)), round(lerp(sy, 0, t))))
        out = span(f, 24, 36)
        if out > 0:
            fr.alpha_composite(Image.new("RGBA", (W, H), (0, 0, 0, round(255 * out))))
        frames.append(fr.convert("RGB"))
    return frames


# Hybrid Ports alone (no ScummVM installed): its list is the first screen, B
# asks to exit as before Systems, and Options has no Start in
ALONE_HINTS = [("ok", "Details"), ("fav", "Favourite"), ("ports", "Repo"), ("back", "Exit")]


# The consoles' game lists (Danik, 2026-11-02; docs/dnklauncher.md, "Game lists"):
# A on a console's banner opens its list, the core itself its first row; a game's
# name from its No-Intro file name, its pictures from libretro-thumbnails (fetched
# here once into THUMBS, never shipped: the project gives no licence). Proposals
# for the review page, not yet approved: console_mockups() draws them.
THUMBS = os.environ.get("DNK_MOCK_THUMBS") or os.path.expanduser("~/MiSTer-dnkLauncher/build/thumbs")
THUMB_URL = "https://raw.githubusercontent.com/libretro-thumbnails/%s/master/%s/%s.png"
THUMB_KINDS = {"box": "Named_Boxarts", "logo": "Named_Logos", "snap": "Named_Snaps", "title": "Named_Titles"}
CONSOLE_CHIPS = ["A-Z", "Recent", "Favourites"]
REGION_ORDER = ("USA", "World", "Europe", "Japan")   # First region: USA (Danik's default), then the others
LANGUAGES = {"En": "English", "Ja": "Japanese", "Fr": "French", "De": "German", "Es": "Spanish", "It": "Italian",
             "Nl": "Dutch", "Sv": "Swedish", "Pt": "Portuguese"}
HANDHELDS = ("gameboy", "gbc", "gba", "gamegear", "lynx", "ngpc", "wonderswan", "wsc")   # their screens' own shape

# What the mock-ups show of a player's games: per console, the files (No-Intro
# names, without the type), the recent ones and the favourites
CONSOLE_SAMPLES = {
    "snes": dict(art="Nintendo - Super Nintendo Entertainment System", ext="sfc", core="SNES", folder="SNES", files=[
        "Super Metroid (Japan, USA) (En,Ja)", "Super Metroid (Europe) (En,Fr,De)", "Chrono Trigger (USA)",
        "Super Mario World (USA)", "Donkey Kong Country (USA) (Rev 2)",
        "Donkey Kong Country (Europe) (En,Fr,De) (Rev 1)", "Legend of Zelda, The - A Link to the Past (USA)",
        "F-Zero (USA)", "Super Mario Kart (USA)", "Mega Man X (USA) (Rev 1)", "Super Castlevania IV (USA)",
        "Street Fighter II Turbo (USA) (Rev 1)", "EarthBound (USA)", "Secret of Mana (USA)",
        "Final Fantasy III (USA) (Rev 1)", "Super Punch-Out!! (USA)", "Kirby Super Star (USA)",
        "Contra III - The Alien Wars (USA)", "Nightmare Busters (World) (Aftermarket) (Unl)",
        "Super Mario World (USA) (Beta)"],
        recent=[("Super Metroid", "today"), ("Chrono Trigger", "yesterday"), ("Super Mario Kart", "3 days ago"),
                ("Donkey Kong Country", "last week")],
        favourites=("Super Metroid", "Chrono Trigger", "EarthBound")),
    "megadrive": dict(art="Sega - Mega Drive - Genesis", ext="md", core="MegaDrive", folder="MegaDrive", files=[
        "Sonic The Hedgehog (USA, Europe)", "Sonic The Hedgehog 2 (World) (Rev A)", "Streets of Rage 2 (USA)",
        "Gunstar Heroes (USA)", "Phantasy Star IV (USA)", "Shining Force (USA)", "Castlevania - Bloodlines (USA)",
        "Comix Zone (USA)", "Earthworm Jim (USA)", "ToeJam _ Earl (USA, Europe, Korea) (En)",
        "Golden Axe (World) (Rev A)", "Mortal Kombat II (World)", "Thunder Force IV (Europe)"],
        recent=[("Sonic The Hedgehog 2", "today")], favourites=("Gunstar Heroes",)),
    "gameboy": dict(art="Nintendo - Game Boy", ext="gb", core="Gameboy", folder="GAMEBOY", files=[
        "Tetris (World) (Rev 1)", "Pokemon - Red Version (USA, Europe) (SGB Enhanced)",
        "Legend of Zelda, The - Link's Awakening (USA, Europe) (Rev 2)", "Super Mario Land (World) (Rev 1)",
        "Kirby's Dream Land (USA, Europe)", "Metroid II - Return of Samus (World)",
        "Wario Land - Super Mario Land 3 (World)", "Dr. Mario (World) (Rev 1)", "Final Fantasy Adventure (USA)"],
        recent=[("Tetris", "today")], favourites=("Metroid II - Return of Samus",)),
}
HIDDEN_TAGS = ("Beta", "Proto", "Demo", "Hack", "Sample", "Kiosk")   # hidden unless Options shows them


def thumb(system, name, kind):
    """A picture of libretro-thumbnails, fetched once into THUMBS; None where it has none"""
    import urllib.error
    import urllib.parse
    import urllib.request
    repo = system.replace(" ", "_")
    path = os.path.join(THUMBS, repo, THUMB_KINDS[kind], name + ".png")
    if not os.path.exists(path) and not os.path.exists(path + ".none"):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        try:
            rel = "%s/%s.png" % (THUMB_KINDS[kind], name)
            for _ in range(4):   # many are git symlinks: GitHub's raw file is then the name they point to
                data = urllib.request.urlopen(THUMB_URL.replace("%s/%s.png", "%s") % (repo, urllib.parse.quote(rel)),
                                              timeout=30).read()
                if data.startswith(b"\x89PNG"):
                    break
                rel = os.path.normpath(os.path.join(os.path.dirname(rel), data.decode("utf-8").strip()))
            else:
                raise urllib.error.HTTPError(rel, 404, "a link's chain too long", None, None)
            with open(path, "wb") as f:
                f.write(data)
        except urllib.error.HTTPError:
            open(path + ".none", "w").close()
    return path if os.path.exists(path) else None


def nointro(name):
    """A No-Intro name's parts: the title (", The" put back in front), the region,
    the languages, the revision and its other tags"""
    m = re.match(r"(.*?)((?: \([^)]*\))*)$", name)
    base, tags = m.group(1), re.findall(r"\(([^)]*)\)", m.group(2))
    t = re.match(r"(.*?), (The|A|An)( - .*)?$", base)
    title = "%s %s%s" % (t.group(2), t.group(1), t.group(3) or "") if t else base
    region = tags[0] if tags else ""
    langs = next((x for x in tags[1:] if re.fullmatch(r"[A-Z][a-z](,[A-Z][a-z])*", x)), "")
    rev = next((x for x in tags if x.startswith("Rev ")), "")
    return dict(title=title.replace(" _ ", " & "), region=region, langs=langs, rev=rev,
                other=[x for x in tags[1:] if x not in (langs, rev)])


def region_rank(region):
    return min((REGION_ORDER.index(r) for r in REGION_ORDER if r in region), default=len(REGION_ORDER))


def console_games(key):
    """The console's games, one per title, the version First region picks first;
    the hidden ones (betas and such) left out"""
    sample = CONSOLE_SAMPLES[key]
    by_title = {}
    for name in sample["files"]:
        p = nointro(name)
        if any(t.startswith(HIDDEN_TAGS) for t in p["other"]):
            continue
        p.update(file="%s.%s" % (name, sample["ext"]), name=name)
        by_title.setdefault(p["title"], []).append(p)
    games = []
    when = dict(sample["recent"])
    for title, versions in by_title.items():
        versions.sort(key=lambda v: v["rev"], reverse=True)   # the latest revision (Rev 2, Rev B) first
        versions.sort(key=lambda v: region_rank(v["region"]))
        v = versions[0]
        games.append(dict(id=key + ":" + title, title=title, versions=versions, system=key, when=when.get(title, ""),
                          art=sample["art"], fav=title in sample["favourites"]))
    return games


def game_name(g, style, v=None):
    v = v or g["versions"][0]
    if style == "region":
        return "%s (%s)" % (g["title"], v["region"])
    if style == "file":
        return v["file"]
    return g["title"]


def game_pic(g, kind, v=None):
    v = v or g["versions"][0]
    path = thumb(g["art"], v["name"], kind)
    if not path and kind != "box":   # another version's, where this one has none
        path = next((p for p in (thumb(g["art"], w["name"], kind) for w in g["versions"]) if p), None)
    return Image.open(path).convert("RGBA") if path else None


def screen_shape(key, pic, w, h):
    """A console's picture as its screen shows it: a television's 4:3 (the pixels as
    wide as the television makes them), a handheld's its own shape, fitted in w x h"""
    if key in HANDHELDS:
        r = min(w / pic.width, h / pic.height)
        return to_linear_resize(pic.convert("RGB"), (round(pic.width * r), round(pic.height * r)))
    ww = min(w, round(h * 4 / 3))
    return to_linear_resize(pic.convert("RGB"), (ww, round(ww * 3 / 4)))


def title_card(F, s, title, size):
    """A game without a picture: its name on the system's colours, its logo faint under it"""
    w, h = size
    im = gradient(w, h, s["c1"], s["c2"], vertical=True)
    mask = fit(system_logo(F, s), w - 40, 28)
    im.alpha_composite(inked(mask, (255, 255, 255), 0.35), ((w - mask.width) // 2, h - 18 - mask.height))
    pic = title_picture(F, title, w - 28, h - 70, 26)
    pic = inked(pic.getchannel("A"), (255, 255, 255))
    im.alpha_composite(pic, ((w - pic.width) // 2, (h - 40 - pic.height) // 2))
    im.putalpha(ImageChops.multiply(im.getchannel("A"), round_mask(w, h, 6)))
    return im


def framed(pic, xy):
    """A picture in the dark frame the screenshots have"""
    out = blank()
    x, y = xy
    ImageDraw.Draw(out).rectangle((x - 2, y - 2, x + pic.width + 1, y + pic.height + 1), fill=(15, 0, 22, 255))
    out.alpha_composite(pic.convert("RGBA"), (x, y))
    return out


def box_max():
    """A box in the preview's panel, at most: the panel less 12 round it"""
    return PREV_X[1] - PREV_X[0] - 24, LIST_Y[1] - LIST_Y[0] - 24


class ConsoleMain(Main):
    """A console's list: first the core alone, then its games; the pane its art as
    Game art says, the title card where a game has none"""
    chips, hints = CONSOLE_CHIPS, MAIN_HINTS

    def __init__(self, F, badge_font, s, mode, cursor_title, art="shot", names="title", family="ps",
                 core_label="%(title)s core"):
        self.F, self.badge_font, self.family, self.mode, self.art, self.shift = F, badge_font, family, mode, art, False
        self.s, self.names = s, names
        self.title = s["title"]
        self.games = console_games(s["key"])
        for g in self.games:   # before the rows are drawn: their stars
            (FAVOURITES.add if g["fav"] else FAVOURITES.discard)(g["id"])
        sample = CONSOLE_SAMPLES[s["key"]]
        self.core_row = dict(id="core", title=core_label % dict(core=sample["core"], title=s["title"]), core=True)
        if mode == "Recent":
            order = [g for t, _ in sample["recent"] for g in self.games if g["title"] == t]
            rows = [("game", game_name(g, names), g, g["when"]) for g in order]
        else:
            of = [g for g in self.games if mode != "Favourites" or g["fav"]]
            rows = [("game", game_name(g, names), g, "")
                    for g in sorted(of, key=lambda g: game_name(g, names).lower())]
        self.rows = [("game", self.core_row["title"], self.core_row, "")] + rows
        self.count = "%d games" % len(self.games)
        self.strips = list_strips(F, self.rows)
        self.cursor = next(n for n, r in enumerate(self.rows) if r[2]["title"] == cursor_title)
        self.first = max(0, min(self.cursor - N_ROWS // 2, len(self.rows) - N_ROWS))

    def frame_layers(self):
        art, self.art = self.art, "shot"   # the pane is its own, below
        L = Main.frame_layers(self)
        self.art = art
        g = self.game()
        L["prev_logo"], L["prev_shot"] = self.pane(g)
        return L

    def preview_logo(self, g, dx=0.0):
        return blank()

    def preview_shot(self, g, xy=None):
        return blank()

    def pane(self, g):
        F, s = self.F, self.s
        pw = PREV_X[1] - PREV_X[0]
        cx, cy = (PREV_X[0] + PREV_X[1]) // 2, (LIST_Y[0] + LIST_Y[1]) // 2
        if g.get("core"):   # the core alone: its card, as on its Details
            card = core_card(F, s)
            return blank(), framed(card, (cx - card.width // 2, cy - card.height // 2))
        if self.art == "cover":
            box = game_pic(g, "box")
            bw, bh = box_max()
            if box:   # as large as the panel takes it, its own shape
                r = min(bw / box.width, bh / box.height)
                pic = to_linear_resize(box.convert("RGB"), (round(box.width * r), round(box.height * r)))
            else:
                pic = title_card(F, s, g["title"], COVER)
            return blank(), framed(pic, (cx - pic.width // 2, cy - pic.height // 2))
        snap, logo = game_pic(g, "snap"), game_pic(g, "logo")
        sx, sy = preview_shot_xy()
        if snap:
            pic = screen_shape(s["key"], snap, *SHOT)
        else:
            pic = title_card(F, s, g["title"], SHOT)
        shot = framed(pic, (cx - pic.width // 2, sy + (SHOT[1] - pic.height) // 2))
        top = blank()
        if logo:
            lp = logo.crop(logo.getbbox())
            r = min((pw - 24) / lp.width, LOGO_H / lp.height)
            lp = lp.resize((max(1, round(lp.width * r)), max(1, round(lp.height * r))), Image.LANCZOS)
        elif snap:   # no logo: the name where it would be
            lp = title_picture(F, g["title"], pw - 24, LOGO_H, 26)
        else:
            lp = None
        if lp:
            top.alpha_composite(lp, (cx - lp.width // 2, sy - 12 - lp.height))
        return top, shot


def console_screen(F, badge_font, key, mode, cursor_title, **kw):
    s = next(x for x in read_systems() if x["key"] == key)
    return compose(ConsoleMain(F, badge_font, s, mode, cursor_title, **kw).frame_layers()).convert("RGB")


def console_details_layers(F, badge_font, s, g, version=0, names="title", family="ps", art="shot"):
    """Details of a console's game: its picture as Game art says (Danik, 2026-11-02:
    the box under Cover, at the picture's height and its own width; else the
    screenshot, or the title screen where it has none), its logo (its name where it
    has none), the facts of the version chosen (Left and Right change it, where
    there are several), the file's below"""
    v = g["versions"][version]
    L = {"ground": layer_ground(), "logo": layer_logo(), "title": layer_title(F, s["title"]),
         "count": layer_count(F, "Details", s["title"]), "badge": layer_badge(badge_font),
         "hints": layer_hints(F, [("ok", "Play"), ("fav", "Favourite"), ("ports", "Repo"), ("back", "Back")],
                              badge_font, family, second=DETAILS_SECOND),
         "shot_panel": layer_panel(D_SHOT_PANEL), "info_panel": layer_panel(D_INFO_PANEL),
         "desc_panel": layer_panel(D_DESC_PANEL)}
    box = game_pic(g, "box", v) if art == "cover" else None
    if box:
        pic = fit_to(box, h=SHOT[1]) if box.width * SHOT[1] <= SHOT[0] * box.height else fit_to(box, w=SHOT[0])
    elif art == "cover":   # the list's 2:3 title card
        pic = title_card(F, s, g["title"], (SHOT[1] * 2 // 3, SHOT[1]))
    else:
        pic = game_pic(g, "snap", v) or game_pic(g, "title", v)
        pic = screen_shape(s["key"], pic, *SHOT) if pic else title_card(F, s, g["title"], SHOT)
    L["shot"] = framed(pic, (D_SHOT[0] + (SHOT[0] - pic.width) // 2, D_SHOT[1] + (SHOT[1] - pic.height) // 2))
    c = Canvas(ground=False)
    x0, y0, x1, y1 = D_INFO_PANEL
    pw = x1 - x0
    bx, by, bw, bh = details_logo_box()
    logo = game_pic(g, "logo", v)
    if logo:
        lp = logo.crop(logo.getbbox())
        r = min(DLOGO[0] / lp.width, DLOGO[1] / lp.height, bw / lp.width)
        lp = lp.resize((max(1, round(lp.width * r)), max(1, round(lp.height * r))), Image.LANCZOS)
    else:
        lp = title_picture(F, g["title"], bw, bh, 26)
    c.picture(lp, (bx + (bw - lp.width) // 2, by + (bh - lp.height) // 2))
    if g["fav"]:
        c.star(x1 - 16, y0 + 16, 7, GOLD)
    ty = by + bh + 8
    layer = c.layer()
    ImageDraw.Draw(layer).line((x0 + 14, ty, x1 - 14, ty), fill=EDGE + (90,))
    c.over(layer)
    langs = ", ".join(LANGUAGES.get(x, x) for x in v["langs"].split(",")) if v["langs"] else \
        {"USA": "English", "Japan": "Japanese"}.get(v["region"], "")
    facts = [("Region", v["region"], None if len(g["versions"]) > 1 else INK), ("Languages", langs or "not named", INK),
             ("Revision", v["rev"][4:] if v["rev"] else "first release", INK),
             ("Last played", g["when"] or "never", INK)]
    lw = max(F.small.getlength(k) for k, val, ink in facts) + 12
    for n, (k, val, ink) in enumerate(facts):
        y = ty + 8 + n * 16
        c.text((x0 + 14, y + 1), k, F.small, DIM)
        if ink is None:     # the version: its arrows and the value between them, as a core's Details
            ax = x0 + 14 + lw
            layer = c.layer()
            ImageDraw.Draw(layer).polygon(((ax + 6, y + 3), (ax, y + 8), (ax + 6, y + 13)), fill=PINK + (255,))
            c.over(layer)
            val = clip(val, F.chip, pw - 28 - lw - 30)
            c.text((ax + 12, y), val, F.chip, INK)
            ex = ax + 12 + F.chip.getlength(val) + 6
            layer = c.layer()
            ImageDraw.Draw(layer).polygon(((ex, y + 3), (ex + 6, y + 8), (ex, y + 13)), fill=PINK + (255,))
            c.over(layer)
            c.text((ex + 12, y + 1), "%d of %d" % (version + 1, len(g["versions"])), F.small, DIM)
        else:
            c.text((x0 + 14 + lw, y), clip(val, F.chip, pw - 28 - lw), F.chip, ink)
    L["info"] = c.im
    c = Canvas(ground=False)
    x0, y0, x1, y1 = D_DESC_PANEL
    sample = CONSOLE_SAMPLES[s["key"]]
    size = {"sfc": "3.0 MB", "md": "2.0 MB", "gb": "1.0 MB"}.get(sample["ext"], "1 MB")
    copy = [("File", v["file"], INK), ("Folder", "games/%s/" % sample["folder"], INK), ("Size", size, INK),
            ("Core", "%s_20260823" % sample["core"], CYAN)]
    lw = max(F.small.getlength(k) for k, val, ink in copy) + 12
    for n, (k, val, ink) in enumerate(copy):   # one under the other: a file's name is long
        cy = y0 + (y1 - y0 - 4 * 18) // 2 + n * 18
        c.text((x0 + 16, cy + 1), k, F.small, DIM)
        c.text((x0 + 16 + lw, cy), clip(val, F.chip, x1 - x0 - 32 - lw), F.chip, ink)
    L["desc"] = c.im
    return L


def console_details_screen(F, badge_font, key, title, version=0, art="shot"):
    s = next(x for x in read_systems() if x["key"] == key)
    g = next(g for g in console_games(key) if g["title"] == title)
    return compose(console_details_layers(F, badge_font, s, g, version, art=art), D_ORDER).convert("RGB")




def console_options_screen(F, badge_font, fonts, cursor, values, running=None):
    """Options on its page Game lists: the two actions showing when they last ran,
    or how far they are"""
    page = next(n for n, p in enumerate(OPTION_PAGES) if p[1] == "cart")
    L = options_layers(F, badge_font, fonts, page, cursor, values)
    # the actions: what they did last, or how far they are, where a value goes
    c = Canvas(ground=False)
    x0, x1, top = OPT_X0, W - M, OPT_LIST[0] + 10
    for n, o in enumerate(page_options(page)):
        if o[1] not in GAME_LIST_ACTIONS:
            continue
        y = top + n * OPT_ROW
        sel = n == cursor
        text = values.get(o[1], "")
        c.text((x1 - 20, y + 15), text, F.small, SEL_INK if sel else DIM, anchor="rm")
        if running and o[1] == running[0]:
            frac = running[1]
            layer = c.layer()
            d = ImageDraw.Draw(layer)
            bx0, bx1, by = x0 + 14, x1 - 20, y + 25   # 2 high, within the row's bar (30)
            d.rounded_rectangle((bx0, by, bx1, by + 1), 1, fill=(SEL_INK if sel else DIM) + (80,))
            d.rounded_rectangle((bx0, by, bx0 + round((bx1 - bx0) * frac), by + 1), 1,
                                fill=(SEL_INK if sel else CYAN) + (255,))
            c.over(layer)
    L["rows"].alpha_composite(c.im)
    return compose(L, OPTIONS_ORDER).convert("RGB")


# Download game art's window (Danik, 2026-11-03): chosen in Options, a window
# over it lists the consoles that have a list (A to Z), each with a tick,
# none ticked at each opening; OK ticks the one under the cursor, Select all or
# none, Start downloads what is ticked and goes back to Options, whose row then
# shows how far it is, Back closes it; chosen again while it runs, a question
# as Exit's: Cancel download? (Danik's picks, 2026-11-03)
PICK = (100, 82, W - 100, 444)           # the window, below the header
PICK_LIST = (PICK[0] + 14, PICK[1] + 52, PICK[2] - 14, PICK[1] + 52 + 10 * ROW + 6)
PICK_ROWS = 10
PICK_MB = 0.4                           # MB a game, about (the test lists 0.49, Danik's Lynx 0.26)
# Danik's consoles as Look for games found them on his MiSTer (2026-11-03):
# title, games, how many have art
PICK_CONSOLES = [("3DO", 161, 0), ("Atari 2600", 450, 0), ("Atari 5200", 70, 0), ("Atari 7800", 61, 0),
                 ("Atari Lynx", 78, 73), ("Game Boy", 555, 0), ("Game Boy Advance", 1001, 0),
                 ("Game Boy Color", 651, 0), ("Game Gear", 288, 0), ("Jaguar", 50, 0), ("Master System", 363, 0),
                 ("Mega Drive", 718, 0), ("Mega-CD", 151, 0), ("NES", 797, 0), ("Neo Geo", 268, 0),
                 ("Nintendo 64", 261, 0), ("PlayStation", 1294, 0), ("Saturn", 264, 0), ("Super Nintendo", 736, 0),
                 ("32X", 31, 0), ("TurboGrafx-16", 389, 0), ("Virtual Boy", 23, 0), ("WonderSwan", 113, 0),
                 ("WonderSwan Color", 73, 0)]
CANCEL_HINTS = (("ok", "Cancel download"), ("back", "Go on"))


def pick_tick(c, x, mid, on, sel):
    """A tick box, 14 square: empty, or filled with its tick"""
    ss = 4
    layer = Image.new("RGBA", (15 * ss, 15 * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    ink = SEL_INK if sel else CYAN if on else DIM
    if on:
        d.rounded_rectangle((0, 0, 14 * ss, 14 * ss), 3 * ss, fill=ink + (255,))
        d.line(((3 * ss, 7.5 * ss), (6 * ss, 10.5 * ss), (11 * ss, 4 * ss)), fill=(PINK if sel else PANEL) + (255,),
               width=2 * ss, joint="curve")
    else:
        d.rounded_rectangle((ss, ss, 13 * ss, 13 * ss), 3 * ss, outline=ink + (255,), width=round(1.5 * ss))
    over = c.layer()
    over.paste(layer.reduce(ss), (x, mid - 7))
    c.over(over)


def pick_summary(ticked):
    games = sum(PICK_CONSOLES[i][1] - PICK_CONSOLES[i][2] for i in ticked)
    if not ticked:
        return "Nothing ticked"
    n = len(ticked)
    mb = games * PICK_MB
    size = "%.1f GB" % (mb / 1000) if mb >= 1000 else "%d MB" % round(mb)
    return "%d console%s, %s games without art: about %s" % (n, "" if n == 1 else "s", f"{games:,}", size)


def layer_art_pick(F, family, cursor, ticked, first):
    """The window: its title, the consoles' rows with their ticks (the cursor's
    bar, the scroll bar), what the ticked ones come to, the buttons"""
    c = Canvas(ground=False)
    x0, y0, x1, y1 = PICK
    c.panel(PICK, alpha=CONFIRM_ALPHA)
    c.text(((x0 + x1) / 2, y0 + 27), "Download game art", F.head, INK, anchor="mm")
    lb = PICK_LIST
    c.panel(lb)
    top = lb[1] + 3
    for k in range(PICK_ROWS):
        i = first + k
        if i >= len(PICK_CONSOLES):
            break
        title, games, have = PICK_CONSOLES[i]
        y = top + k * ROW
        sel = i == cursor
        if sel:
            c.bar((lb[0] + 1, y, lb[2] - SB_W, y + ROW), panel=lb)
        mid = y + ROW // 2
        pick_tick(c, lb[0] + 12, mid, i in ticked, sel)
        c.text((lb[0] + 36, mid), title, F.row, SEL_INK if sel else INK, anchor="lm")
        what = f"{have:,} of {games:,} have art" if have else f"{games:,} games"
        c.text((lb[2] - SB_W - 10, mid), what, F.small, SEL_INK if sel else DIM, anchor="rm")
    c.over(scroll_bar(lb, PICK_ROWS, len(PICK_CONSOLES), first))
    c.text(((x0 + x1) / 2, lb[3] + 18), pick_summary(ticked), F.small, INK if ticked else DIM, anchor="mm")
    where = places(family)
    hints = (("ok", "Tick"), ("sort", "All or none"), ("options", "Download"), ("back", "Close"))
    probe = Canvas(ground=False)
    width = sum(button(probe, F, family, where[a], 0, 0) + 4 + round(F.chip.getlength(w)) for a, w in hints) + 18 * 3
    x, y = round((x0 + x1 - width) / 2), y1 - 32
    for action, what in hints:
        x += button(c, F, family, where[action], x, y) + 4
        c.text((x, y + 9), what, F.chip, INK, anchor="lm")
        x += round(F.chip.getlength(what)) + 18
    return c.im


def layer_cancel(F, family="ps"):
    """The question while the download runs, as Exit's"""
    c = Canvas(ground=False)
    x0, y0, x1, y1 = CONFIRM
    c.panel(CONFIRM, alpha=CONFIRM_ALPHA)
    c.text(((x0 + x1) / 2, y0 + 38), "Cancel download?", F.head, INK, anchor="mm")
    where = places(family)
    probe = Canvas(ground=False)
    width = sum(button(probe, F, family, where[a], 0, 0) + 4 + round(F.chip.getlength(w)) for a, w in CANCEL_HINTS) + 24
    x, y = round((x0 + x1 - width) / 2), y0 + 70
    for action, what in CANCEL_HINTS:
        x += button(c, F, family, where[action], x, y) + 4
        c.text((x, y + 9), what, F.chip, INK, anchor="lm")
        x += round(F.chip.getlength(what)) + 24
    return c.im


def art_pick_mockups(F, badge_font, fonts, values):
    """Download game art's window, Options before and after, the question"""
    shots, anims = {}, {}
    page = next(n for n, p in enumerate(OPTION_PAGES) if p[1] == "cart")
    row = next(n for n, o in enumerate(page_options(page)) if o[1] == "artdl")
    v = dict(values, scan="today: 8,889 games", artdl="73 of 8,889 have art")
    idle = console_options_screen(F, badge_font, fonts, row, v).convert("RGBA")
    shots["artpick_0_options"] = idle.convert("RGB")
    p0 = layer_art_pick(F, "ps", 0, set(), 0)
    shots["artpick_1_open"] = confirm_frame(idle, p0, CONFIRM_FIELDS)
    anims["artpick_open"] = [confirm_frame(idle, p0, f) for f in range(CONFIRM_FIELDS + 1)]
    ticked = {4, 11, 18}
    shots["artpick_2_ticked"] = confirm_frame(idle, layer_art_pick(F, "ps", 11, ticked, 4), CONFIRM_FIELDS)
    shots["artpick_3_scrolled"] = confirm_frame(idle, layer_art_pick(F, "ps", 18, ticked, 14), CONFIRM_FIELDS)
    shots["artpick_4_all"] = confirm_frame(idle, layer_art_pick(F, "ps", 18, set(range(len(PICK_CONSOLES))), 14),
                                           CONFIRM_FIELDS)
    shots["artpick_4b_switch"] = confirm_frame(idle, layer_art_pick(F, "switch", 11, ticked, 4), CONFIRM_FIELDS)
    running = console_options_screen(F, badge_font, fonts, row, dict(v, artdl="Mega Drive: 212 of 1,437"),
                                     running=("artdl", 0.15)).convert("RGBA")
    shots["artpick_5_running"] = running.convert("RGB")
    shots["artpick_6_cancel"] = confirm_frame(running, layer_cancel(F), CONFIRM_FIELDS)
    anims["artpick_cancel"] = [confirm_frame(running, layer_cancel(F), f) for f in range(CONFIRM_FIELDS + 1)]
    return shots, anims


def card_shapes_sheet(F, badge_font):
    """The Carousel's question, drawn flat on the launcher's ground: boxes of three
    shapes (the SNES's landscape, the Mega Drive's portrait, the Game Boy's nearly
    square) at one height, each in one card of 2:3, or at one width"""
    picks = [("snes", "Super Metroid"), ("megadrive", "Gunstar Heroes"), ("gameboy", "Metroid II - Return of Samus"),
             ("megadrive", "Comix Zone")]
    pics = [game_pic(next(g for g in console_games(k) if g["title"] == t), "box") for k, t in picks]
    sheet = layer_ground().copy()
    d = ImageDraw.Draw(sheet)
    labels = ("A: one height, each box its own width", "B: one card of 2:3, the box fitted in it",
              "C: one width, each box its own height")
    for row in range(3):
        y0 = 34 + row * 150
        d.text((24, y0 - 24), labels[row], font=F.chip, fill=INK + (255,))
        ims = []
        for pic in pics:
            if row == 0:
                ims.append(fit_to(pic, h=104))
            elif row == 1:
                card = Image.new("RGBA", (78, 117), (15, 0, 22, 255))
                b = fit_to(pic, w=78) if pic.width / pic.height > 78 / 117 else fit_to(pic, h=117)
                card.alpha_composite(b.convert("RGBA"), ((78 - b.width) // 2, (117 - b.height) // 2))
                ims.append(card)
            else:
                ims.append(fit_to(pic, w=82))
        x = (W - sum(im.width for im in ims) - 16 * (len(ims) - 1)) // 2
        for im in ims:
            sheet.alpha_composite(im.convert("RGBA"), (x, y0 + (120 - im.height) // 2))
            x += im.width + 16
    return sheet.convert("RGB")


def fit_to(pic, w=None, h=None):
    r = (h / pic.height) if h else (w / pic.width)
    return to_linear_resize(pic.convert("RGB"), (max(1, round(pic.width * r)), max(1, round(pic.height * r))))


def console_mockups(F, badge_font, fonts, values):
    """The game lists' proposals for the review page"""
    shots = {}
    shots["list_shot"] = console_screen(F, badge_font, "snes", "A-Z", "Super Metroid")
    shots["list_cover"] = console_screen(F, badge_font, "snes", "A-Z", "Super Metroid", art="cover")
    shots["list_core"] = console_screen(F, badge_font, "snes", "A-Z", "Super Nintendo core")
    shots["list_core_snes"] = console_screen(F, badge_font, "snes", "A-Z", "SNES core", core_label="%(core)s core")
    shots["list_core_none"] = console_screen(F, badge_font, "snes", "A-Z", "No game: the core alone",
                                             core_label="No game: the core alone")
    shots["list_nopic"] = console_screen(F, badge_font, "snes", "A-Z", "Nightmare Busters")
    shots["list_nopic_cover"] = console_screen(F, badge_font, "snes", "A-Z", "Nightmare Busters", art="cover")
    shots["list_region"] = console_screen(F, badge_font, "snes", "A-Z", "Super Metroid", names="region")
    shots["list_file"] = console_screen(F, badge_font, "snes", "A-Z", "Super Metroid", names="file")
    shots["list_recent"] = console_screen(F, badge_font, "snes", "Recent", "Super Metroid")
    shots["list_md_cover"] = console_screen(F, badge_font, "megadrive", "A-Z", "Gunstar Heroes", art="cover")
    shots["list_md_shot"] = console_screen(F, badge_font, "megadrive", "A-Z", "Gunstar Heroes")
    shots["list_gb_shot"] = console_screen(F, badge_font, "gameboy", "A-Z", "Metroid II - Return of Samus")
    shots["list_gb_cover"] = console_screen(F, badge_font, "gameboy", "A-Z", "Metroid II - Return of Samus", art="cover")
    shots["details"] = console_details_screen(F, badge_font, "snes", "Super Metroid")
    shots["details_v2"] = console_details_screen(F, badge_font, "snes", "Super Metroid", 1)
    shots["details_one"] = console_details_screen(F, badge_font, "snes", "Chrono Trigger")
    shots["details_nopic"] = console_details_screen(F, badge_font, "snes", "Nightmare Busters")
    shots["details_gb"] = console_details_screen(F, badge_font, "gameboy", "Metroid II - Return of Samus")
    shots["details_cover"] = console_details_screen(F, badge_font, "snes", "Super Metroid", art="cover")
    shots["details_cover_one"] = console_details_screen(F, badge_font, "snes", "Chrono Trigger", art="cover")
    shots["details_cover_md"] = console_details_screen(F, badge_font, "megadrive", "Gunstar Heroes", art="cover")
    shots["details_cover_gb"] = console_details_screen(F, badge_font, "gameboy", "Metroid II - Return of Samus",
                                                       art="cover")
    shots["details_cover_nopic"] = console_details_screen(F, badge_font, "snes", "Nightmare Busters", art="cover")
    v = dict(values, names="Title alone", region="USA", extras="Off", scan="3 days ago: 1,204 games",
             artdl="1,187 of 1,204 have art")
    shots["options"] = console_options_screen(F, badge_font, fonts, 0, v)
    shots["options_scan"] = console_options_screen(F, badge_font, fonts, 3, dict(v, scan="SNES: 412 games so far"),
                                                   running=("scan", 0.35))
    shots["options_artdl"] = console_options_screen(F, badge_font, fonts, 4, dict(v, artdl="212 of 1,204"),
                                                    running=("artdl", 0.18))
    shots["card_shapes"] = card_shapes_sheet(F, badge_font)
    rows = mock_rows()   # Systems: the open banner of a console with a list, its count where its year and type are
    n = next(i for i, r in enumerate(rows) if r["key"] == "snes")
    shots["systems_today"] = systems_screen(F, badge_font, rows, n)
    rows[n]["line2"] = "17 games"
    shots["systems_count"] = systems_screen(F, badge_font, rows, n)
    rows[n]["line2"] = "1990 Console  17 games"
    shots["systems_count_both"] = systems_screen(F, badge_font, rows, n)
    for g in [g for k in CONSOLE_SAMPLES for g in console_games(k)]:
        FAVOURITES.discard(g["id"])
    return shots


def systems_mockups(F, badge_font, ports, fonts, values):
    """Every screen and animation of Systems: (screens, animations)"""
    sv = load_scummvm()
    sv_port = next(p for p in ports if p["engine"] == "ScummVM")
    launcher = dict(sv_port["games"][0], launcher=True)
    hports = [p for p in ports if p["engine"] != "ScummVM"]   # its entry is ScummVM's list's first row
    rows = mock_rows()
    rows_out = mock_rows(missing=True)
    idx = {r["key"]: n for n, r in enumerate(rows_out)}
    screens, anims = {}, {}
    screens["systems"] = systems_screen(F, badge_font, rows, 0)
    screens["systems_scummvm"] = systems_screen(F, badge_font, rows, 1)
    screens["systems_favourite"] = systems_screen(F, badge_font, rows, idx["c64"])
    screens["systems_core"] = systems_screen(F, badge_font, rows, idx["nes"])
    screens["systems_shift"] = systems_screen(F, badge_font, rows, idx["nes"], shift=True)
    screens["systems_shift_special"] = systems_screen(F, badge_font, rows, 0, shift=True)
    shown = [dict(r, hidden=r["key"] == "nes") for r in rows]
    screens["systems_hidden_shown"] = systems_screen(F, badge_font, shown, idx["nes"])
    screens["systems_shift_unhide"] = systems_screen(F, badge_font, shown, idx["nes"], shift=True)
    screens["systems_consoles"] = systems_screen(F, badge_font, mock_rows("Consoles"), 2, chip="Consoles")
    screens["systems_computers"] = systems_screen(F, badge_font, mock_rows("Computers"), 2, chip="Computers")
    screens["systems_hybrid"] = systems_screen(F, badge_font, mock_rows("Hybrid"), 0, chip="Hybrid")
    last = len(rows) - 1
    screens["systems_end"] = systems_screen(F, badge_font, rows_out, last)
    screens["systems_not_installed"] = systems_screen(F, badge_font, rows_out, last + 3)
    screens["systems_not_installed_end"] = systems_screen(F, badge_font, rows_out, len(rows_out) - 1)
    for style in ("still", "breathe", "swap", "both"):
        anims["ring_" + style] = anim_ring(F, badge_font, rows, style)
    anims["systems_cursor"] = anim_systems_cursor(F, badge_font, rows)
    anims["systems_chip"] = anim_chip(F, badge_font, rows, mock_rows("Consoles"), "All", "Consoles")
    anims["system_pick"] = anim_system_pick(F, badge_font, rows, idx["nes"])
    screens["systems_favourites"] = systems_screen(F, badge_font, mock_rows(CHIP_STAR), 0, chip=CHIP_STAR)
    screens["systems_favourites_none"] = systems_screen(F, badge_font, [], 0, chip=CHIP_STAR)
    screens["systems_picked"] = systems_screen(F, badge_font, rows, idx["nes"], pick=(PICK_DX, {}))
    by = {r["key"]: r for r in rows_out}
    for key in ("nes", "c64", "amiga"):
        screens["core_%s" % key] = core_details_screen(F, badge_font, by[key])
    screens["core_gameboy_version"] = core_details_screen(F, badge_font, by["gameboy"], 1)
    screens["core_mac_version"] = core_details_screen(F, badge_font, by["mac"], 3)
    library = len(OPTION_PAGES) - 1
    missing = OPTION_PAGES[library][2].index("missing")
    screens["options_missing"] = options_screen(F, badge_font, fonts, library, missing, values)
    logos = OPTION_PAGES[library][2].index("logos")
    screens["options_logos"] = options_screen(F, badge_font, fonts, library, logos, dict(values, logos="In colour"))
    LOGO_COLOUR[0] = True
    screens["systems_colour"] = systems_screen(F, badge_font, rows, idx["nes"])
    screens["systems_colour_c64"] = systems_screen(F, badge_font, rows, idx["c64"])
    for theme in ("lavender", "amber"):
        apply_theme(theme)
        screens["systems_colour_%s" % theme] = systems_screen(F, badge_font, rows, idx["nes"])
    apply_theme("purple")
    LOGO_COLOUR[0] = False
    for theme in ("lavender", "amber"):
        apply_theme(theme)
        screens["systems_%s" % theme] = systems_screen(F, badge_font, rows, idx["nes"])
        screens["core_nes_%s" % theme] = core_details_screen(F, badge_font, by["nes"])
    apply_theme("purple")
    anims["core_details"] = anim_core_details(F, badge_font, rows, idx["nes"])
    anims["core_back"] = anim_core_details(F, badge_font, rows, idx["nes"], back=True)
    anims["core_start"] = anim_core_start(F, badge_font, by["nes"])
    for mode, cur in (("A-Z", "monkey-cd"), ("Engine", "sky"), ("Publisher", "lure"), ("Year", "atlantis"),
                      ("Recent", "monkey-cd")):
        m = ScummMain(F, badge_font, sv, launcher, mode, cur)
        screens["scummvm_%s" % mode.lower()] = compose(m.frame_layers()).convert("RGB")
    sm = ScummMain(F, badge_font, sv, launcher, "A-Z", "ScummVM")
    screens["scummvm_launcher"] = compose(sm.frame_layers()).convert("RGB")
    D = details_layers(F, badge_font, launcher, sv_port)
    D.update(title=layer_title(F, "ScummVM"), count=layer_count(F, "Details", "ScummVM"),
             hints=layer_hints(F, [("ok", "Play"), ("ports", "Repo"), ("back", "Back")], badge_font, "ps", second=DETAILS_SECOND))
    screens["scummvm_details_launcher"] = compose(D, D_ORDER).convert("RGB")
    for gid in ("monkey-cd", "atlantis"):
        screens["scummvm_details_%s" % gid] = compose(
            sv_details_layers(F, badge_font, sv[[g["id"] for g in sv].index(gid)]), D_ORDER).convert("RGB")
    screens["hybrid_list"] = main_screen(F, badge_font, hports, "A-Z", "doom2")
    library = len(OPTION_PAGES) - 1
    start = OPTION_PAGES[library][2].index("start")
    screens["options_start"] = options_screen(F, badge_font, fonts, library, start, values)
    alone = Main(F, badge_font, hports, "A-Z", "doom2")
    alone.hints = ALONE_HINTS
    screens["alone_list"] = compose(alone.frame_layers()).convert("RGB")
    screens["alone_options"] = options_screen(F, badge_font, fonts, library, start, values, hide=("start",))
    hm = Main(F, badge_font, hports, "A-Z", "doom2")
    sm = ScummMain(F, badge_font, sv, launcher, "A-Z", "monkey-cd")
    anims["open_hybrid"] = anim_system_open(F, badge_font, rows, hm, 0)
    anims["open_scummvm"] = anim_system_open(F, badge_font, rows, sm, 1)
    anims["back_scummvm"] = anim_system_open(F, badge_font, rows, sm, 1, back=True)
    anims["start_systems"] = anim_start_systems(F, badge_font, rows)
    return screens, anims


def save_animation(frames, path, slow=1):
    """An animated WebP at the field rate (slow: that many times slower), with
    a pause before and after"""
    durations = [round((i + 1) * FIELD_MS * slow) - round(i * FIELD_MS * slow) for i in range(len(frames))]
    durations[0] += 500
    durations[-1] += 1200
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=durations, loop=0,
                   lossless=False, quality=80, method=6)


def export_layers(fonts_dir, out, repos):
    """--layers: for the page that tries grounds, every theme's main screen,
    Details and Options without their ground (RGBA), and the themes' colours"""
    import json
    ports = load_ports(repos)
    badge = cozette(out)
    fonts = {key: (FONTS[key][0].replace(" SemiBold", "").replace(" ExtraBold", "").replace(" Bold", "")
                   .replace(" Medium", ""), Fonts(fonts_dir, key)) for key in FONTS}
    F = fonts["chakra"][1]
    values = {"font": "chakra", "colours": "purple", "pattern": "Checker", "intensity": "25%", "strength": "Even",
              "direction": "Down and right", "speed": "Normal", "filter": "Light", "margin": f"{MARGIN} pixels", "buttons": "Auto: PlayStation",
              "pointer": "Arrow", "layout": "List", "art": "Logo + Screenshot", "hidden": "Off", "start": "Last System", "missing": "Off", "logos": "One colour", "sort": "A-Z",
              "names": "Title alone", "region": "USA", "extras": "Off", "scan": "never", "artdl": "not yet"}
    colours = {}
    for theme in THEMES:
        apply_theme(theme)
        colours[theme] = dict(label=THEMES[theme]["label"], top=THEMES[theme]["bg"][0],
                              bottom=THEMES[theme]["bg"][1])
        m = Main(F, badge, ports, "A-Z", "doom2")
        L = m.frame_layers()
        L.pop("ground")
        g = m.game()
        D = details_layers(F, badge, g, m.port(g), m.family)
        D.pop("ground")
        o = options_layers(F, badge, fonts, 0, 1, dict(values, colours=theme), m.family)
        o.pop("ground")
        for name, im in (("main", compose(L)), ("details", compose(D, D_ORDER)),
                         ("options", compose(o, OPTIONS_ORDER[:4] + OPTIONS_ORDER[4:]))):
            path = os.path.join(out, "%s_%s.png" % (theme, name))
            im.save(path, optimize=True)
            print(path)
    apply_theme("purple")
    with open(os.path.join(out, "themes.json"), "w") as f:
        json.dump(colours, f, indent=1)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--layers":
        if len(sys.argv) < 4:
            sys.exit("usage: dnklauncher_mockup.py --layers <fonts dir> <out dir> [repositories...]")
        os.makedirs(sys.argv[3], exist_ok=True)
        return export_layers(sys.argv[2], sys.argv[3], sys.argv[4:] or sorted(glob.glob(os.path.expanduser("~/MiSTer-*"))))
    only_systems = len(sys.argv) > 1 and sys.argv[1] == "--systems"
    only_consoles = len(sys.argv) > 1 and sys.argv[1] == "--consoles"
    only_artpick = len(sys.argv) > 1 and sys.argv[1] == "--artpick"
    if only_systems or only_consoles or only_artpick:
        del sys.argv[1]
    if len(sys.argv) < 3:
        sys.exit("usage: dnklauncher_mockup.py [--systems | --consoles | --artpick] <fonts dir> <out dir> [repositories...]")
    fonts_dir, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    repos = sys.argv[3:] or sorted(glob.glob(os.path.expanduser("~/MiSTer-*")))
    ports = load_ports(repos)
    badge = cozette(out)
    fonts = {key: (FONTS[key][0].replace(" SemiBold", "").replace(" ExtraBold", "").replace(" Bold", "")
                   .replace(" Medium", ""), Fonts(fonts_dir, key)) for key in FONTS}
    values = {"font": "chakra", "colours": "purple", "pattern": "Checker", "intensity": "25%", "strength": "Even",
              "direction": "Down and right", "speed": "Normal", "filter": "Light", "margin": f"{MARGIN} pixels", "buttons": "Auto: PlayStation",
              "pointer": "Arrow", "layout": "List", "art": "Logo + Screenshot", "hidden": "Off", "start": "Last System", "missing": "Off", "logos": "One colour", "sort": "A-Z",
              "names": "Title alone", "region": "USA", "extras": "Off", "scan": "never", "artdl": "not yet"}
    if only_systems:
        return save_all(out, *systems_mockups(fonts["chakra"][1], badge, ports, fonts, values))
    if only_artpick:   # Download game art's window (not yet approved: not in the rest)
        return save_all(out, *art_pick_mockups(fonts["chakra"][1], badge, fonts, values))
    if only_consoles:   # the game lists' proposals (not yet approved: not in the rest)
        return save_all(out, console_mockups(fonts["chakra"][1], badge, fonts, values), {})
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
    screens["cover_list"] = main_screen(F, badge, ports, "A-Z", "doom2", art="cover")
    screens["shift_hide"] = main_screen(F, badge, ports, "A-Z", "doom2", shift=True)
    HIDDEN.update(("doom2", "heretic"))
    screens["hidden_shown"] = main_screen(F, badge, ports, "A-Z", "heretic")
    screens["shift_unhide"] = main_screen(F, badge, ports, "A-Z", "doom2", shift=True)
    HIDDEN.clear()
    screens["confirm_exit"] = confirm_screen(F, badge, ports)
    screens["confirm_exit_switch"] = confirm_screen(F, badge, ports, "switch")
    screens["wait_ports"] = wait_screen(F, badge, ports)
    screens["options_rail"] = options_screen(F, badge, fonts, 0, 0, values, focus="rail")
    screens["options_rail_library"] = options_screen(F, badge, fonts, len(OPTION_PAGES) - 1, 0, values, focus="rail")
    screens["options_font"] = options_screen(F, badge, fonts, 0, 0, values)
    screens["options_colours"] = options_screen(F, badge, fonts, 0, 1, values)
    screens["options_pattern"] = options_screen(F, badge, fonts, 1, 0, values)
    screens["options_pattern_off"] = options_screen(F, badge, fonts, 1, 0, dict(values, pattern="Off"))
    screens["options_buttons"] = options_screen(F, badge, fonts, 3, 0, values)
    screens["options_art"] = options_screen(F, badge, fonts, len(OPTION_PAGES) - 1, 1, dict(values, art="Cover"))
    screens["options_hidden"] = options_screen(F, badge, fonts, len(OPTION_PAGES) - 1, 4, dict(values, hidden="On"))
    screens["options_scroll"] = options_screen(F, badge, fonts, len(OPTION_PAGES) - 1, 7, values)   # the list scrolled to its end
    screens["options_about"] = options_screen(F, badge, fonts, len(OPTION_PAGES) - 1, 7, values, focus="about")
    for theme in THEMES:     # the colour schemes
        apply_theme(theme)
        screens["theme_%s_main" % theme] = main_screen(F, badge, ports, "A-Z", "doom2")
        screens["theme_%s_details" % theme] = details_screen(F, badge, ports, "doom2")
        screens["theme_%s_options" % theme] = options_screen(F, badge, fonts, 0, 1, dict(values, colours=theme))
    apply_theme("purple")
    default = GROUND["pattern"]
    for pattern in PATTERNS + (None,):   # the option Pattern (GROUND)
        GROUND["pattern"] = pattern
        screens["ground_%s" % (pattern or "off")] = main_screen(F, badge, ports, "A-Z", "doom2")
    GROUND["pattern"] = default
    screens.update(pointer_screens(F, badge, ports))
    sys_screens, sys_anims = systems_mockups(F, badge, ports, fonts, values)
    screens.update(sys_screens)
    animations = {"start": anim_start(F, badge, ports), "cursor": anim_cursor(F, badge, ports),
                  "sort": anim_sort(F, badge, ports), "details": anim_details(F, badge, ports),
                  "back": anim_details(F, badge, ports, back=True),
                  "options": anim_options(F, badge, ports, fonts, values),
                  "options_cursor": anim_options_cursor(F, badge, ports, fonts, values),
                  "options_page": anim_options_page(F, badge, fonts, values), "play": anim_play(F, badge, ports),
                  "confirm": anim_confirm(F, badge, ports), "wait": anim_wait(F, badge, ports)}
    animations.update(sys_anims)
    save_all(out, screens, animations)


def save_all(out, screens, animations):
    for name, im in screens.items():
        path = os.path.join(out, name + ".png")
        im.save(path)
        print(path)
    for name, frames in animations.items():
        for slow, suffix in ((1, ""), (4, "_slow")):
            path = os.path.join(out, "anim_%s%s.webp" % (name, suffix))
            save_animation(frames, path, slow)
            print(path, len(frames), "frames")


if __name__ == "__main__":
    main()
