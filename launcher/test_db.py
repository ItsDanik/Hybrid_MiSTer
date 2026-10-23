#!/usr/bin/env python3
# Test the database of the hybrid ports before distribution.py push, the way a
# player meets it: Downloader, from its source, installs into a folder that
# stands for the SD card, and the launcher's own screen, built for this PC,
# shows what the database offers, in a window of its own.
#
#   1. The card of a player of the database on GitHub (the checkout's
#      origin/db), with every port of it ticked, installed from it by
#      Downloader. --fresh: a new player instead, with the drop-in file as it
#      is shipped (filter = launcher), and no step 1.
#   2. update_all with the database to push (the local db branch): what the
#      player has is brought up to date, and nothing else comes.
#   3. danik_hybrid_ports from the Scripts menu: the screen, the ports new in
#      this database on offer and not ticked. Tick them and press X: Apply
#      runs Downloader again, and its screen says where their game files go.
#
# After each Downloader run the card is compared with the database: every file
# its filter takes, with the database's bytes, and nothing of a port left
# over (an old core, a file a new release no longer has).
#
# The databases are the db.json.zip that players get, byte for byte but for
# their URLs, turned to http://dnk.invalid/: Downloader refuses URLs of local
# addresses, so it goes through HTTP_PROXY to this script's server, which
# gives out the files from the checkout's git objects, at the commit each URL
# names. The card's Scripts/update.sh stands in for update_all: it points the
# drop-in file at the server again (the launcher writes its own db_url back
# with every Apply) and runs Downloader. The launcher runs on the PC with
# DANIK_HYBRID_ROOT and DANIK_HYBRID_TMP, and its daemon is stopped at the end.
#
# The window is the screen as on the MiSTer, with the pad's keys: the arrows,
# A or Enter ticks, X or Space applies, B or Escape leaves, L / R or Page Up /
# Down. --auto drives it alone (ticks the new ports, applies) and saves it in
# the work folder: screen.png as it opens, new-<Engine>.png on each new port
# before it is ticked, ticked.png, and applied.png when Downloader is done.
#
# usage: launcher/test_db.py [--fresh] [--auto] [--work DIR] [--downloader DIR] [<checkout>]
#   <checkout>         of dnk_Distribution_MiSTer (~/dnk_Distribution_MiSTer)
#   --work DIR         the card, the launcher's /tmp, the screen's build
#                      ($TMPDIR/dnk_db_test); card and tmp are made again each time
#   --downloader DIR   a checkout of MiSTer-devel/Downloader_MiSTer (default:
#                      cloned into the work folder the first time)
import argparse
import hashlib
import http.server
import io
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.parse
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
DB_ID = "danik_hybrid_ports"
RAW = "https://raw.githubusercontent.com/ItsDanik/dnk_Distribution_MiSTer/"
LOCAL = "http://dnk.invalid/"
DOWNLOADER_GIT = "https://github.com/MiSTer-devel/Downloader_MiSTer.git"
# what the launcher's screen writes into the drop-in file (write_section)
CHOSEN = "# chosen with danik_hybrid_ports.sh (Scripts menu): its screen writes the filter"
UI = "Scripts/.danik_hybrid_ports/danik_hybrid_ports_ui"
CATALOGUE = "Scripts/.danik_hybrid_ports/catalogue.txt"
# the framebuffer the screen draws on: 320x200 three times
FB_W, FB_H = 960, 600
KEY_UP, KEY_DOWN, KEY_OK, KEY_APPLY = b"\x1b[A", b"\x1b[B", b"\r", b" "


def die(msg):
    sys.exit("test_db.py: " + msg)


def norm(tag):
    return tag.lower().replace("-", "").replace("_", "")


def git(checkout, *args):
    r = subprocess.run(["git", "-C", checkout, *args], capture_output=True)
    if r.returncode:
        die("git %s: %s" % (" ".join(args), r.stderr.decode().strip()))
    return r.stdout


def read_ports(text):
    return [tuple(l.split("|", 1)) for l in text.splitlines() if l.strip() and not l.startswith("#")]


# ── the databases ───────────────────────────────────────────────────

def load_db(checkout, ref):
    db = json.loads(zipfile.ZipFile(io.BytesIO(git(checkout, "show", ref + ":db.json.zip"))).read("db.json"))
    shas = {e["url"][len(RAW):].split("/", 1)[0] for e in db["files"].values() if e["url"].startswith(RAW)}
    if len(shas) != 1 or len(db["files"]) != sum(e["url"].startswith(RAW) for e in db["files"].values()):
        die("%s: its files are not all of one commit of %s" % (ref, RAW))
    return db, shas.pop()


def served(db):
    db = json.loads(json.dumps(db))
    for e in db["files"].values():
        e["url"] = LOCAL + e["url"][len(RAW):]
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("db.json", json.dumps(db, indent=1, sort_keys=True))
    return buf.getvalue()


class Server(http.server.ThreadingHTTPServer):
    # the proxy Downloader goes through: the full URL is the path
    def __init__(self, checkout):
        self.checkout, self.db, self.served = checkout, b"", 0

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(h):
                u = urllib.parse.urlsplit(h.path)
                path = urllib.parse.unquote(u.path)
                data = None
                if u.hostname == "dnk.invalid" and path == "/db.json.zip":
                    data = self.db
                elif u.hostname == "dnk.invalid":
                    m = re.fullmatch(r"/([0-9a-f]{40})/(files/.+)", path)
                    if m:
                        r = subprocess.run(["git", "-C", checkout, "cat-file", "blob", "%s:%s" % m.groups()],
                                           capture_output=True)
                        data = r.stdout if r.returncode == 0 else None
                if data is None:
                    h.send_error(404)
                    return
                h.send_response(200)
                h.send_header("Content-Length", str(len(data)))
                h.end_headers()
                h.wfile.write(data)
                self.served += 1

            def log_message(h, *a):
                pass

        super().__init__(("127.0.0.1", 0), Handler)


# ── the card ────────────────────────────────────────────────────────

def section_filter(card):
    with open(os.path.join(card, "downloader_%s.ini" % DB_ID)) as f:
        m = re.search(r"^[ \t]*filter[ \t]*=[ \t]*(.*)$", f.read(), re.M)
    return m.group(1).strip() if m else ""


def downloader(card):
    print("\n── update_all (Downloader, %s alone) ──" % DB_ID, flush=True)
    r = subprocess.run(["bash", os.path.join(card, "Scripts/update.sh"), "--run-only", DB_ID])
    if r.returncode:
        die("Downloader failed (exit code %d)" % r.returncode)


def check_card(card, db, filt, swapped=()):
    # what the filter takes, with the database's bytes, and no file of ours
    # that it does not take
    terms = {norm(t) for t in filt.split()}
    want = {p for p, e in db["files"].items() if terms & {norm(t) for t in e["tags"]}}
    bad = []
    for p in sorted(want):
        if p in swapped:
            continue
        try:
            with open(os.path.join(card, p), "rb") as f:
                data = f.read()
        except FileNotFoundError:
            bad.append("missing: " + p)
            continue
        if hashlib.md5(data).hexdigest() != db["files"][p]["hash"]:
            bad.append("not the database's: " + p)
    ours = []
    for top in ("_Other", "games/dnk", "Scripts/.danik_hybrid_ports"):
        for d, _, names in os.walk(os.path.join(card, top)):
            ours += [os.path.relpath(os.path.join(d, n), card) for n in names]
    if os.path.exists(os.path.join(card, "Scripts/danik_hybrid_ports.sh")):
        ours.append("Scripts/danik_hybrid_ports.sh")
    bad += ["left over: " + p for p in sorted(set(ours) - want - set(swapped))]
    engines = sorted({p.split("/")[2] for p in want if p.startswith("games/dnk/") and p.count("/") > 2
                      and not p.startswith("games/dnk/SoundFonts/")}, key=str.lower)
    print("── the card, filter = %s: %d files of the database%s" % (filt, len(want), ", ports: " + ", ".join(engines) if engines else ""))
    for b in bad:
        print("   " + b)
    print("   " + ("FAIL" if bad else "ok"), flush=True)
    return not bad


# ── the screen ──────────────────────────────────────────────────────

def fb_frame(fb):
    try:
        with open(fb, "rb") as f:
            return f.read(FB_W * FB_H * 4)
    except FileNotFoundError:
        return b""


def fb_image(data):
    from PIL import Image
    return Image.frombuffer("RGB", (FB_W, FB_H), data, "raw", "BGRX", 0, 1)


def wait_frame(fb, unlike=None, timeout=60):
    # a frame drawn, other than unlike
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        f = fb_frame(fb)
        if len(f) == FB_W * FB_H * 4 and f.strip(b"\0") and f != unlike:
            return f
        time.sleep(0.1)
    die("the screen showed nothing new in %d seconds" % timeout)


def wait_still(fb, seconds=3, timeout=900):
    # the run screen's busy bar moves until the command is done
    end = time.monotonic() + timeout
    last, since = fb_frame(fb), time.monotonic()
    while time.monotonic() < end:
        time.sleep(0.25)
        f = fb_frame(fb)
        if f != last:
            last, since = f, time.monotonic()
        elif time.monotonic() - since >= seconds:
            return f
    die("the screen was still working after %d seconds" % timeout)


def auto(proc, fb, rows, work):
    def send(b):
        proc.stdin.write(b)
        proc.stdin.flush()
        time.sleep(0.05)
    wait_frame(fb)
    time.sleep(0.5)
    fb_image(fb_frame(fb)).save(os.path.join(work, "screen.png"))
    sel, shots = 0, ["screen.png"]
    for engine, r in rows:
        for _ in range(r - sel):
            send(KEY_DOWN)
        sel = r
        # the new port as the player finds it, not ticked
        time.sleep(0.3)
        shots.append("new-%s.png" % engine)
        fb_image(fb_frame(fb)).save(os.path.join(work, shots[-1]))
        send(KEY_OK)
    time.sleep(0.5)
    ticked = fb_frame(fb)
    fb_image(ticked).save(os.path.join(work, "ticked.png"))
    send(KEY_APPLY)
    wait_frame(fb, unlike=ticked)
    fb_image(wait_still(fb)).save(os.path.join(work, "applied.png"))
    # the end of the input closes the run screen
    proc.stdin.close()
    proc.wait()
    print("── the screen: %s" % ", ".join(os.path.join(work, n) for n in shots + ["ticked.png", "applied.png"]))


def window(proc, fb):
    import tkinter as tk
    from PIL import ImageTk
    keys = {"Up": KEY_UP, "Down": KEY_DOWN, "Right": b"\x1b[C", "Left": b"\x1b[D",
            "Return": KEY_OK, "KP_Enter": KEY_OK, "a": KEY_OK, "space": KEY_APPLY, "x": KEY_APPLY,
            "Escape": b"\x1b", "b": b"\x1b", "Tab": b"\t", "y": b"\t",
            "Prior": b"\x1b[5~", "l": b"\x1b[5~", "Next": b"\x1b[6~", "r": b"\x1b[6~",
            "Home": b"\x1b[H", "End": b"\x1b[F"}
    root = tk.Tk()
    root.title("danik_hybrid_ports (test of the database)")
    root.resizable(False, False)
    label = tk.Label(root, bd=0, width=FB_W, height=FB_H, bg="black")
    label.pack()
    shown = {"frame": None, "photo": None}

    def key(e):
        if e.keysym in keys and not proc.stdin.closed:
            try:
                proc.stdin.write(keys[e.keysym])
                proc.stdin.flush()
            except BrokenPipeError:
                pass

    def close():
        # the end of the input: the screen leaves, or closes once its work is done
        if not proc.stdin.closed:
            proc.stdin.close()
        root.destroy()

    def tick():
        if proc.poll() is not None:
            root.destroy()
            return
        f = fb_frame(fb)
        if len(f) == FB_W * FB_H * 4 and f != shown["frame"]:
            shown["frame"], shown["photo"] = f, ImageTk.PhotoImage(fb_image(f))
            label.configure(image=shown["photo"], width=FB_W, height=FB_H)
        root.after(33, tick)

    root.bind("<Key>", key)
    root.protocol("WM_DELETE_WINDOW", close)
    root.after(0, tick)
    root.focus_force()
    root.mainloop()
    proc.wait()


def new_rows(card, engines):
    # the list's lines of the new ports: a port, then each of its games
    rows, n, last = [], 0, None
    with open(os.path.join(card, CATALOGUE), encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or "|" not in line:
                continue
            engine = line.split("|", 1)[0]
            if engine != last:
                if engine in engines:
                    rows.append((engine, n))
                n, last = n + 1, engine
            n += 1
    return rows


# ── the test ────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Test the database of the hybrid ports before distribution.py push")
    ap.add_argument("checkout", nargs="?", default=os.path.expanduser("~/dnk_Distribution_MiSTer"))
    ap.add_argument("--fresh", action="store_true", help="a new player: only the launcher to begin with")
    ap.add_argument("--auto", action="store_true", help="tick the new ports and apply without a window")
    ap.add_argument("--work", default=os.path.join(os.environ.get("TMPDIR", "/tmp"), "dnk_db_test"))
    ap.add_argument("--downloader", help="a checkout of Downloader_MiSTer")
    a = ap.parse_args()
    checkout, work = os.path.abspath(a.checkout), os.path.abspath(a.work)
    card, tmp, fb = os.path.join(work, "card"), os.path.join(work, "tmp"), os.path.join(work, "fb")

    # what goes out, against what is out
    head = git(checkout, "rev-parse", "HEAD").decode().strip()
    new_db, new_sha = load_db(checkout, "db")
    if new_sha != head:
        die("the db branch is the database of %s, not of main (%s): publish again" % (new_sha[:7], head[:7]))
    if git(checkout, "status", "--porcelain", "--", "ports.txt", "shared.txt", "files", "README.md").strip():
        print("(the checkout has changes that are not committed: the database is that of %s)" % head[:7])
    new_ports = read_ports(git(checkout, "show", "HEAD:ports.txt").decode())
    old_ports, old_db, old_sha = [], None, None
    if git(checkout, "branch", "-r", "--list", "origin/db").strip():
        old_db, old_sha = load_db(checkout, "origin/db")
        old_ports = read_ports(git(checkout, "show", old_sha + ":ports.txt").decode())
    titles = {}
    for line in git(checkout, "show", "HEAD:files/" + CATALOGUE).decode().splitlines():
        if "|" in line and not line.startswith("#"):
            e, _, t = line.split("|", 2)
            titles.setdefault(e, []).append(t)
    old = dict(old_ports)
    added = [e for e, _ in new_ports if e not in old]
    print("The database on GitHub: %s" % ("main %s, %d ports" % (old_sha[:7], len(old_ports)) if old_sha else "none"))
    print("The database to push:   main %s, %d ports" % (head[:7], len(new_ports)))
    for e, z in new_ports:
        if e not in old:
            print("  new      %-13s %s" % (e, ", ".join(titles.get(e, []))))
        elif old[e] != z:
            print("  updated  %-13s %s -> %s" % (e, old[e], z))
    for e in old:
        if e not in dict(new_ports):
            print("  removed  %s" % e)
    if a.fresh or not old_db:
        old_db = None

    # Downloader, and the screen for this PC
    src = a.downloader or os.path.join(work, "Downloader_MiSTer")
    os.makedirs(work, exist_ok=True)
    if not a.downloader and not os.path.isdir(src):
        subprocess.run(["git", "clone", "-q", "--depth", "1", DOWNLOADER_GIT, src], check=True)
    if not os.path.isfile(os.path.join(src, "src", "__main__.py")):
        die("%s is no checkout of Downloader_MiSTer" % src)
    print("Downloader %s" % git(src, "log", "-1", "--format=%h %cs").decode().strip())
    with open(os.path.join(HERE, "danik_hybrid_ports.sh")) as f:
        version = re.search(r"^VERSION=(\d+)$", f.read(), re.M).group(1)
    ui = os.path.join(work, "danik_hybrid_ports_ui")
    subprocess.run(["gcc", "-O2", "-Wall", "-DLAUNCHER_VERSION=" + version, "-o", ui,
                    os.path.join(HERE, "ui", "danik_hybrid_ports_ui.c")], check=True)

    server = Server(checkout)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    for d in (card, tmp):
        shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(card, "Scripts"))
    os.makedirs(tmp)
    with open(os.path.join(card, "downloader.ini"), "w") as f:
        f.write("[mister]\n")
    shutil.copy(os.path.join(src, "src", "pc_launcher.py"), os.path.join(card, "downloader_pc_launcher.py"))
    with open(os.path.join(card, "Scripts/update.sh"), "w") as f:
        f.write("#!/bin/bash\n"
                "# update_all of launcher/test_db.py: Downloader from its source, through the test's server\n"
                "sed -i 's|^db_url[ \\t]*=.*|db_url = %sdb.json.zip|' %s/downloader_%s.ini\n"
                "PC_LAUNCHER=%s/downloader_pc_launcher.py PC_LAUNCHER_NO_WAIT=1 HTTP_PROXY=http://127.0.0.1:%d \\\n"
                "    exec python3 %s/src \"$@\"\n"
                % (LOCAL, card, DB_ID, card, server.server_address[1], src))
    drop_in = os.path.join(card, "downloader_%s.ini" % DB_ID)
    env = dict(os.environ, DANIK_HYBRID_ROOT=card, DANIK_HYBRID_TMP=tmp,
               DANIK_HYBRID_UI_ARGS="--fb %s --geometry %dx%dx32 --no-mouse" % (fb, FB_W, FB_H))
    launcher = os.path.join(card, "Scripts/danik_hybrid_ports.sh")
    ok = True
    try:
        if old_db:
            print("\n══ 1. A player of the database on GitHub, with all its ports ══")
            with open(drop_in, "w") as f:
                f.write("%s\n[%s]\ndb_url = %sdb.json.zip\nfilter = launcher %s\n"
                        % (CHOSEN, DB_ID, LOCAL, " ".join(e.lower() for e, _ in old_ports)))
            server.db = served(old_db)
            downloader(card)
            ok &= check_card(card, old_db, section_filter(card))
            # its launcher set up, as the player's is
            subprocess.run(["bash", launcher, "takeover"], env=env)
        else:
            shutil.copy(os.path.join(HERE, "downloader_%s.ini" % DB_ID), drop_in)

        print("\n══ 2. update_all with the database to push ══")
        server.db = served(new_db)
        downloader(card)
        ok &= check_card(card, new_db, section_filter(card))

        print("\n══ 3. danik_hybrid_ports from the Scripts menu ══")
        installed = {e for e, _ in new_ports if os.path.isdir(os.path.join(card, "games/dnk", e))}
        offer = [e for e, _ in new_ports if e not in installed]
        if added:
            print("New in this database: %s" % "; ".join("%s (%s)" % (e, ", ".join(titles.get(e, []))) for e in added))
        if not a.auto:
            print("The screen is in its own window: arrows move, A or Enter ticks, X or Space applies,\n"
                  "B or Escape leaves.%s" % (" Tick the new ports and apply." if added else ""), flush=True)
        shutil.copy(ui, os.path.join(card, UI))
        if os.path.exists(fb):
            os.remove(fb)
        proc = subprocess.Popen(["bash", launcher], stdin=subprocess.PIPE, env=env)
        if a.auto:
            auto(proc, fb, new_rows(card, set(added if old_db else offer)), work)
        else:
            window(proc, fb)
        # Downloader puts its own screen program back; the PC's was in its place
        filt = section_filter(card)
        print()
        ok &= check_card(card, new_db, filt, swapped={UI})
        if a.auto:
            terms = set(filt.lower().split())
            missing = [e for e in (added if old_db else offer) if e.lower() not in terms]
            if missing:
                print("   not ticked by the screen: %s\n   FAIL" % ", ".join(missing))
                ok = False
    finally:
        subprocess.run(["bash", launcher, "stop"], env=env, capture_output=True)
        server.shutdown()
    print("\n%s (%d files served; the card: %s)" % ("PASS" if ok else "FAIL", server.served, card))
    return 0 if ok else 1


sys.exit(main())
