#!/usr/bin/env python3
# The screenshot of the launcher's screen for the ports' READMEs (their
# section "The launcher", hybrid/docs/readme.md): danik_hybrid_ports_ui,
# built for this PC, draws its screen as it opens into a file, with the
# settings of the launcher as it is and the games of the database's
# catalogue, all ticked. Made again whenever the screen changes.
#
# usage: launcher/ui/screenshot.py [<checkout>] [--out docs/launcher.png]
#   <checkout>   of dnk_Distribution_MiSTer (~/dnk_Distribution_MiSTer), for
#                its catalogue
import argparse
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(os.path.dirname(HERE))
CATALOGUE = "files/Scripts/.danik_hybrid_ports/catalogue.txt"
# the framebuffer: 320x200 three times, as test_db.py has it
FB_W, FB_H = 960, 600


def die(msg):
    sys.exit("screenshot.py: " + msg)


def main():
    ap = argparse.ArgumentParser(description="The launcher's screen for the READMEs")
    ap.add_argument("checkout", nargs="?", default=os.path.expanduser("~/dnk_Distribution_MiSTer"))
    ap.add_argument("--out", default=os.path.join(TOP, "docs", "launcher.png"))
    a = ap.parse_args()
    from PIL import Image

    with open(os.path.join(TOP, "launcher", "danik_hybrid_ports.sh")) as f:
        script = f.read()
    version = re.search(r"^VERSION=(\d+)$", script, re.M).group(1)
    layouts = re.search(r'^LAYOUTS="(.*?)"$', script, re.M | re.S).group(1).splitlines()
    try:
        with open(os.path.join(a.checkout, CATALOGUE), encoding="utf-8") as f:
            games = [l.rstrip("\n").split("|", 2) for l in f if "|" in l and not l.startswith("#")]
    except FileNotFoundError:
        die("no %s in %s" % (CATALOGUE, a.checkout))

    with tempfile.TemporaryDirectory() as tmp:
        ui, fb = os.path.join(tmp, "ui"), os.path.join(tmp, "fb")
        subprocess.run(["gcc", "-O2", "-Wall", "-DLAUNCHER_VERSION=" + version, "-o", ui,
                        os.path.join(HERE, "danik_hybrid_ports_ui.c")], check=True)
        table = os.path.join(tmp, "table")
        with open(table, "w", encoding="utf-8") as f:
            f.write("S\tlauncher\t1\tLauncher\n")
            f.write("C\tmenu\t0\tMenu entries\t%s\n" % "|".join(l.split("|")[2] for l in layouts))
            for engine, game, title in games:
                f.write("G\t%s\t%s\t1\t%s\n" % (engine, game, title))
        # the input stays open while the screen is up: its end leaves it
        proc = subprocess.Popen([ui, "main", "--fb", fb, "--geometry", "%dx%dx32" % (FB_W, FB_H), "--no-mouse",
                                 "--table", table, "--out", table + ".out"], stdin=subprocess.PIPE)
        end, frame = time.monotonic() + 10, b""
        while time.monotonic() < end:
            time.sleep(0.2)
            if os.path.exists(fb):
                with open(fb, "rb") as f:
                    frame = f.read(FB_W * FB_H * 4)
                if len(frame) == FB_W * FB_H * 4 and frame.strip(b"\0"):
                    time.sleep(0.5)
                    with open(fb, "rb") as f:
                        frame = f.read(FB_W * FB_H * 4)
                    break
        proc.stdin.close()
        proc.wait()
        if len(frame) != FB_W * FB_H * 4:
            die("the screen drew nothing")
    Image.frombuffer("RGB", (FB_W, FB_H), frame, "raw", "BGRX", 0, 1).save(a.out, optimize=True)
    print("%s: launcher version %s, %d games" % (a.out, version, len(games)))


main()
