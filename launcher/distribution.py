#!/usr/bin/env python3
# The database of the hybrid ports, danik_hybrid_ports: what update_all
# installs through the drop-in file downloader_danik_hybrid_ports.ini. It is
# published from a checkout of ItsDanik/dnk_Distribution_MiSTer, which holds
#
#   ports.txt   the ports in the database, a line each: <Engine>|<zip>. A port
#               is in it only once it has been published, never because it
#               has a release: unannounced ports stay out of the database and
#               off the launcher's screen.
#   files/      what the database installs, as on the SD card: each published
#               port's release zip extracted (its menu entries, core, games/
#               dnk/<Engine>/), the launcher and its screen in Scripts/, and
#               the catalogue, Scripts/.danik_hybrid_ports/catalogue.txt.
#   shared.txt  the files several ports bring, a line each: <path>|<Engine>,...
#               (the ports that bring it): GeneralUser GS in games/dnk/
#               SoundFonts/, which every port with MIDI has in its zip
#               (hybrid/tools/soundfont.sh). Stored once; every port that
#               brings it must bring the same bytes, and it goes when the
#               last of them goes.
#   README.md   for players, with where each game's files go: made again
#               with every publish and remove
#
# and, on its branch "db", db.json.zip, the database itself (the db_url of the
# drop-in file), whose files are served from the commit of main they are in.
#
# usage: distribution.py init <checkout>
#        distribution.py publish <checkout> [<zip>...] a port in, or a new release of one;
#                                                       without a zip: only the launcher
#        distribution.py remove <checkout> <Engine>...  a port out
#        distribution.py check <checkout>
#        distribution.py db <checkout> <base url> <out>  only the database file, for tests
#        distribution.py push <checkout>
#
# publish and remove commit on main and make the db branch again, locally;
# push sends both to GitHub. Nothing reaches players before that.
#
# A zip is a port's release zip from its ./package.sh: _Other/dnk-<title>.mgl,
# _Other/dnk/dnk-<Engine>_YYYYMMDD.rbf, games/dnk/<Engine>/ with games.txt.
# Its Scripts/ and games/dnk/danik_hybrid_cores.sh are not used: the
# database's launcher, screen and launcher before are always this repository's (launcher/danik_hybrid_ports.sh, the screen built with
# launcher/ui/build.sh), made again with every publish and remove, so a new
# launcher reaches players without a port being packaged again. The copy of
# the launcher a zip has in games/dnk/<Engine>/ may be older (the newest
# installed is the one that runs), but not newer than this one, and with the
# same VERSION it must be the same file: one packaged before a change that
# kept the VERSION (never released) is packaged again.
#
# Tags (hybrid/README.md, "Distribution"), as Downloader compares them: lower
# case, without - and _. The launcher, its screen, the catalogue and the
# launcher before (games/dnk/danik_hybrid_cores.sh, version 3, which hands a
# running version 2 over to the new one): launcher. A port's core and games/dnk/<Engine>/: <engine> and the tag of
# each of its games, <engine><game>. A menu entry: <engine><game> and
# <engine>. A shared file (games/dnk/SoundFonts/): the tags of the cores
# and of the games of every port that brings it. A folder: the tags of
# everything in it.
#
# Where the menu entries go is the player's choice (the launcher's screen,
# "Menu entries"): each _Other/dnk-<title>.mgl of a zip is in the database
# once for every layout of LAYOUTS, the same file under the same URL. An
# entry of layout L has the tags mgl<L>, mgl<L><engine> and
# mgl<L><engine><game>; those of the default layout also the two above, so a
# filter of before (or filter = launcher) gets the default layout alone. The
# launcher writes, for layout L, the plain tags of what is ticked, the same
# with mgl<L> in front, and !mgl<X> for every other layout: Downloader takes
# a file one of the positive terms names, unless a ! term names it too.
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.parse
import zipfile

REPO = "ItsDanik/dnk_Distribution_MiSTer"
DB_ID = "danik_hybrid_ports"
RAW = "https://raw.githubusercontent.com/%s/" % REPO
HERE = os.path.dirname(os.path.abspath(__file__))
LAUNCHER = "Scripts/danik_hybrid_ports.sh"
LAUNCHER_DIR = "Scripts/.danik_hybrid_ports"
CATALOGUE = LAUNCHER_DIR + "/catalogue.txt"
# the last version of the launcher before, which hands a running one over
BRIDGE = "games/dnk/danik_hybrid_cores.sh"
# files that several ports bring, stored once (shared.txt)
SHARED_DIR = "games/dnk/SoundFonts"
# the player's in that folder, never a port's: the MIDI Player writes it
PLAYERS = {SHARED_DIR + "/slots.txt"}
# in a zip, not in the database: Downloader does not install drop-in files
NOT_SHIPPED = {"downloader_danik_hybrid_ports.ini"}
# The layouts of the menu entries: the name in their tags, and what comes
# before <title>.mgl. The launcher has the same list (its LAYOUTS).
LAYOUTS = [("other", "_Other/dnk-"),
           ("folder", "_Other/_Hybrid Ports/"),
           ("root", "_Hybrid Ports/")]
# what the ports have shipped from the start: a player who never chooses
# keeps it
DEFAULT_LAYOUT = "other"
ENTRY_PREFIX = dict(LAYOUTS)[DEFAULT_LAYOUT]
README = """# dnk_Distribution_MiSTer

The hybrid ports of [Hybrid_MiSTer](https://github.com/ItsDanik/Hybrid_MiSTer) for update_all: open source
ports of PC games that run on the MiSTer's ARM CPU, with a small FPGA core for their picture, sound and
controls.

1. Copy [downloader_danik_hybrid_ports.ini](https://raw.githubusercontent.com/ItsDanik/Hybrid_MiSTer/main/launcher/downloader_danik_hybrid_ports.ini)
   to the root of your SD card (Downloader 2.4 or newer, which update_all keeps up to date).
2. Run update_all. It installs the launcher, danik_hybrid_ports, in Scripts.
3. Run danik_hybrid_ports from the Scripts menu, tick the games you want and press X: Apply.

The game data is not included: copy the files from your own copy of each game into its port's folder on
the SD card, or into the same folder on a USB drive (`/media/usb0/games/dnk/<Engine>/`), where the port
looks first. Each port's README.txt, which comes with it into the folder on the SD card, says which files
go where; without them, the game says so when it is loaded.
%s
| Games | Your game files go in | Which files |
|---|---|---|
%s
`ports.txt` lists the ports in the database; `files/` is what it installs. Published with
`launcher/distribution.py` of Hybrid_MiSTer; the database itself is on the `db` branch.
"""


def die(msg):
    sys.exit("distribution.py: " + msg)


def norm(tag):
    return tag.lower().replace("-", "").replace("_", "")


def git(checkout, *args, data=None):
    return subprocess.run(["git", "-C", checkout] + list(args), input=data, check=True,
                          stdout=subprocess.PIPE).stdout


def read_lines(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as f:
        return [l.strip() for l in f if l.strip() and not l.startswith("#")]


def read_ports(checkout):
    ports = []
    for line in read_lines(os.path.join(checkout, "ports.txt")):
        engine, zipname = line.split("|", 1)
        ports.append((engine, zipname))
    return ports


def write_ports(checkout, ports):
    with open(os.path.join(checkout, "ports.txt"), "w", encoding="utf-8") as f:
        f.write("# The ports in the database, a line each: <Engine>|<release zip>.\n"
                "# Written by launcher/distribution.py of Hybrid_MiSTer.\n")
        for engine, zipname in sorted(ports, key=lambda p: p[0].lower()):
            f.write("%s|%s\n" % (engine, zipname))


def read_shared(checkout):
    """{path: [engines]} of the files several ports bring"""
    shared = {}
    for line in read_lines(os.path.join(checkout, "shared.txt")):
        path, engines = line.split("|", 1)
        shared[path] = [e for e in engines.split(",") if e]
    return shared


def write_shared(checkout, shared):
    with open(os.path.join(checkout, "shared.txt"), "w", encoding="utf-8") as f:
        f.write("# The files several ports bring, stored once, a line each: <path>|<Engine>,...\n"
                "# Written by launcher/distribution.py of Hybrid_MiSTer.\n")
        for path in sorted(shared):
            f.write("%s|%s\n" % (path, ",".join(sorted(shared[path], key=str.lower))))


def drop_shared(checkout, shared, engine):
    """`engine` no longer brings its shared files: those no port brings go"""
    files = os.path.join(checkout, "files")
    for path in list(shared):
        if engine in shared[path]:
            shared[path].remove(engine)
            if not shared[path]:
                del shared[path]
                remove_paths(files, [path])


def games_of(files, engine):
    """A port's games from its games.txt: [(id, title)]"""
    path = os.path.join(files, "games/dnk", engine, "games.txt")
    games = [tuple(l.split("|", 1)) for l in read_lines(path)]
    if not games:
        die("%s has no games (games/dnk/%s/games.txt): package it again with the current hybrid/" % (engine, engine))
    return games


def mgl_target(data):
    """(engine, set name or None) of a menu entry"""
    text = data.decode("utf-8", "replace")
    rbf = re.search(r"<rbf>\s*_Other/dnk/dnk-([^<\s]+)\s*</rbf>", text)
    setname = re.search(r"<setname[^>]*>\s*([^<\s]+)\s*</setname>", text)
    return (rbf.group(1) if rbf else None), (setname.group(1) if setname else None)


def entry_paths(path):
    """{layout: path} of the menu entry _Other/dnk-<title>.mgl of a zip"""
    title = path[len(ENTRY_PREFIX):]
    return {layout: prefix + title for layout, prefix in LAYOUTS}


def entry_source(path):
    """The file in files/ a path of the database is: a menu entry of another
    layout is the one of the zip"""
    if path.endswith(".mgl"):
        for _, prefix in LAYOUTS:
            if path.startswith(prefix) and "/" not in path[len(prefix):]:
                return ENTRY_PREFIX + path[len(prefix):]
    return path


def engine_paths(files, engine):
    """Every file of a port in the tree"""
    out = []
    for path in walk(files):
        if path.startswith("games/dnk/%s/" % engine) or \
                re.fullmatch(r"_Other/dnk/dnk-%s_\d{8}\.rbf" % re.escape(engine), path):
            out.append(path)
        elif path.startswith("_Other/dnk-") and path.endswith(".mgl"):
            with open(os.path.join(files, path), "rb") as f:
                if mgl_target(f.read())[0] == engine:
                    out.append(path)
    return out


def walk(files):
    out = []
    for root, _, names in os.walk(files):
        for name in names:
            out.append(os.path.relpath(os.path.join(root, name), files).replace(os.sep, "/"))
    return sorted(out)


def remove_paths(files, paths):
    for path in paths:
        os.remove(os.path.join(files, path))
    # empty folders go too
    for root, dirs, names in sorted(os.walk(files), key=lambda w: -len(w[0])):
        if root != files and not os.listdir(root):
            os.rmdir(root)


def framework_launcher():
    with open(os.path.join(HERE, "danik_hybrid_ports.sh"), "rb") as f:
        return f.read()


def launcher_version(data):
    m = re.search(rb"^VERSION=(\d+)$", data, re.M)
    return int(m.group(1)) if m else 0


def put_launcher(files):
    """The framework's launcher and its screen into the tree"""
    os.makedirs(os.path.join(files, LAUNCHER_DIR), exist_ok=True)
    with open(os.path.join(files, LAUNCHER), "wb") as f:
        f.write(framework_launcher())
    os.chmod(os.path.join(files, LAUNCHER), 0o755)
    shutil.copyfile(os.path.join(HERE, "danik_hybrid_cores.sh"), os.path.join(files, BRIDGE))
    os.chmod(os.path.join(files, BRIDGE), 0o755)
    # built where package.sh builds it (the toolchain sees the framework only)
    subprocess.run(["sh", os.path.join(HERE, "ui", "build.sh")], check=True, stdout=subprocess.DEVNULL)
    ui = os.path.join(files, LAUNCHER_DIR, "danik_hybrid_ports_ui")
    shutil.copyfile(os.path.join(HERE, "ui", "danik_hybrid_ports_ui"), ui)
    os.chmod(ui, 0o755)


# ── checks and the catalogue ────────────────────────────────────────

def tags_of(files, ports, shared):
    """{path: [tags]} for every file of the tree, or die if it does not add up"""
    engines = {e for e, _ in ports}
    tags = {}
    game_tags = {}
    seen = {}
    for engine in engines:
        etag = norm(engine)
        game_tags[engine] = [norm(engine + "_" + gid) for gid, _ in games_of(files, engine)]
        layout_tags = [norm("mgl%s_%s" % (l, x)) for l, _ in LAYOUTS
                       for x in [engine] + [engine + "_" + gid for gid, _ in games_of(files, engine)]]
        for t in [etag] + game_tags[engine] + layout_tags:
            if t in seen or t == "launcher" or t in [norm("mgl" + l) for l, _ in LAYOUTS]:
                die("the tag %s of %s is %s's as well (tags are compared without - and _)" % (t, engine, seen.get(t, "the launcher's or a layout's")))
            seen[t] = engine
    entries = {e: set() for e in engines}
    for path in walk(files):
        if path in (LAUNCHER, BRIDGE) or path.startswith(LAUNCHER_DIR + "/"):
            tags[path] = ["launcher"]
            continue
        if path.startswith(SHARED_DIR + "/"):
            if path not in shared:
                die("%s is not in shared.txt" % path)
            for engine in shared[path]:
                if engine not in engines:
                    die("%s is brought by %s, which is not in ports.txt" % (path, engine))
            tags[path] = sorted({t for e in shared[path] for t in [norm(e)] + game_tags[e]})
            continue
        m = re.fullmatch(r"games/dnk/([^/]+)/.+", path) or re.fullmatch(r"_Other/dnk/dnk-(.+)_\d{8}\.rbf", path)
        if m:
            engine = m.group(1)
            if engine not in engines:
                die("%s belongs to %s, which is not in ports.txt" % (path, engine))
            tags[path] = [norm(engine)] + game_tags[engine]
            continue
        if re.fullmatch(r"_Other/dnk-[^/]+\.mgl", path):
            with open(os.path.join(files, path), "rb") as f:
                engine, setname = mgl_target(f.read())
            if engine not in engines:
                die("the menu entry %s loads %s, which is not in ports.txt" % (path, engine))
            games = games_of(files, engine)
            if setname is None:
                if len(games) != 1:
                    die("the menu entry %s has no set name, but %s has %d games" % (path, engine, len(games)))
                gid = games[0][0]
            elif setname.startswith(engine + "_"):
                gid = setname[len(engine) + 1:]
            else:
                die("the menu entry %s has the set name %s, not %s_<game>" % (path, setname, engine))
            if gid not in [g for g, _ in games]:
                die("the menu entry %s is for %s_%s, which is not in %s's games.txt" % (path, engine, gid, engine))
            if gid in entries[engine]:
                die("two menu entries for %s_%s" % (engine, gid))
            entries[engine].add(gid)
            for layout, lpath in entry_paths(path).items():
                t = [norm("mgl" + layout), norm("mgl%s_%s" % (layout, engine)), norm("mgl%s_%s_%s" % (layout, engine, gid))]
                if layout == DEFAULT_LAYOUT:
                    t = [norm(engine + "_" + gid), norm(engine)] + t
                tags[lpath] = t
            continue
        die("%s is no file of a port or of the launcher" % path)
    for engine in engines:
        if not any(re.fullmatch(r"_Other/dnk/dnk-%s_\d{8}\.rbf" % re.escape(engine), p) for p in tags):
            die("%s has no core (_Other/dnk/dnk-%s_YYYYMMDD.rbf)" % (engine, engine))
        missing = [g for g, _ in games_of(files, engine) if g not in entries[engine]]
        if missing:
            die("%s has no menu entry for %s" % (engine, ", ".join(missing)))
    for path in shared:
        if path not in tags:
            die("%s of shared.txt is not in files/" % path)
    if LAUNCHER not in tags:
        die("there is no launcher (%s)" % LAUNCHER)
    return tags


def catalogue_text(files, ports):
    lines = ["# The games of the ports in the database, for the launcher's screen:",
             "# <Engine>|<Game>|<title>. Made by launcher/distribution.py of Hybrid_MiSTer."]
    for engine, _ in sorted(ports, key=lambda p: p[0].lower()):
        for gid, title in games_of(files, engine):
            lines.append("%s|%s|%s" % (engine, gid, title))
    return "\n".join(lines) + "\n"


def readme_text(files, ports):
    """README.md: what is said to players, with a line for each published
    port: its games, where their files go and the README.txt that says which"""
    rows = []
    for engine, _ in sorted(ports, key=lambda p: p[0].lower()):
        folder = "games/dnk/%s/" % engine
        if not os.path.isfile(os.path.join(files, folder, "README.txt")):
            die("%s has no %sREADME.txt" % (engine, folder))
        rows.append("| %s | `/media/fat/%s` | [README.txt](files/%sREADME.txt) |\n"
                    % (", ".join(t for _, t in games_of(files, engine)), folder, urllib.parse.quote(folder)))
    font = ""
    if os.path.isfile(os.path.join(files, SHARED_DIR, "GeneralUser-GS.sf2")):
        font = ("\nThe ports whose games play MIDI music bring the SoundFont [GeneralUser GS](https://www.schristiancollins.com/generaluser.php)\n"
                "by S. Christian Collins, in `/media/fat/%s/` with its licence: their cores' synthesizer plays\n"
                "the music with it. Your own `.sf2` files can go in the same folder.\n" % SHARED_DIR)
    return README % (font, "".join(rows))


def write_readme(checkout, ports):
    with open(os.path.join(checkout, "README.md"), "w", encoding="utf-8") as f:
        f.write(readme_text(os.path.join(checkout, "files"), ports))


def write_catalogue(files, ports):
    os.makedirs(os.path.join(files, LAUNCHER_DIR), exist_ok=True)
    with open(os.path.join(files, CATALOGUE), "w", encoding="utf-8") as f:
        f.write(catalogue_text(files, ports))


def check(checkout):
    files = os.path.join(checkout, "files")
    ports = read_ports(checkout)
    tags = tags_of(files, ports, read_shared(checkout))
    with open(os.path.join(files, LAUNCHER), "rb") as f:
        if f.read() != framework_launcher():
            die("%s is not the launcher of this Hybrid_MiSTer" % LAUNCHER)
    with open(os.path.join(files, BRIDGE), "rb") as f, open(os.path.join(HERE, "danik_hybrid_cores.sh"), "rb") as g:
        if f.read() != g.read():
            die("%s is not the one of this Hybrid_MiSTer" % BRIDGE)
    with open(os.path.join(files, CATALOGUE), encoding="utf-8") as f:
        if f.read() != catalogue_text(files, ports):
            die("%s is not that of ports.txt" % CATALOGUE)
    with open(os.path.join(checkout, "README.md"), encoding="utf-8") as f:
        if f.read() != readme_text(files, ports):
            die("README.md is not that of ports.txt")
    return tags


# ── the database ────────────────────────────────────────────────────

def make_db(checkout, base_url, out):
    files = os.path.join(checkout, "files")
    tags = check(checkout)
    db_files = {}
    folders = {}
    for path, t in sorted(tags.items()):
        source = entry_source(path)
        with open(os.path.join(files, source), "rb") as f:
            data = f.read()
        db_files[path] = {
            "hash": hashlib.md5(data).hexdigest(),
            "size": len(data),
            "url": base_url + urllib.parse.quote(source),
            "tags": t,
        }
        # a folder has the tags of everything in it
        parts = path.split("/")[:-1]
        for i in range(1, len(parts) + 1):
            folder = "/".join(parts[:i])
            folders.setdefault(folder, set()).update(t)
    db = {
        "v": 1,
        "db_id": DB_ID,
        "timestamp": int(time.time()),
        "files": db_files,
        "folders": {f: {"tags": sorted(t)} for f, t in sorted(folders.items())},
        "default_options": {"filter": "launcher"},
    }
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("db.json", json.dumps(db, indent=1, sort_keys=True))
    with open(out, "wb") as f:
        f.write(buf.getvalue())
    return db


def commit(checkout, message):
    git(checkout, "add", "-A", "ports.txt", "shared.txt", "files", "README.md")
    if git(checkout, "status", "--porcelain").strip():
        git(checkout, "commit", "-q", "-m", message)
    sha = git(checkout, "rev-parse", "HEAD").decode().strip()
    out = os.path.join(git(checkout, "rev-parse", "--absolute-git-dir").decode().strip(), "db.json.zip")
    db = make_db(checkout, RAW + sha + "/files/", out)
    # the db branch: one commit with db.json.zip alone, made again each time
    with open(out, "rb") as f:
        blob = git(checkout, "hash-object", "-w", "--stdin", data=f.read()).decode().strip()
    tree = git(checkout, "mktree", data=("100644 blob %s\tdb.json.zip\n" % blob).encode()).decode().strip()
    dbc = git(checkout, "commit-tree", tree, "-m", "Database for %s" % sha[:7]).decode().strip()
    git(checkout, "update-ref", "refs/heads/db", dbc)
    os.remove(out)
    engines = sorted({e for e, _ in read_ports(checkout)}, key=str.lower)
    print("main %s, db %s: %d files, ports: %s" % (sha[:7], dbc[:7], len(db["files"]), ", ".join(engines) or "none"))
    print("Nothing is on GitHub yet: distribution.py push %s" % checkout)


# ── commands ────────────────────────────────────────────────────────

def cmd_init(checkout):
    os.makedirs(os.path.join(checkout, "files"), exist_ok=True)
    if not os.path.isdir(os.path.join(checkout, ".git")):
        subprocess.run(["git", "init", "-q", "-b", "main", checkout], check=True)
    if not os.path.exists(os.path.join(checkout, "ports.txt")):
        write_ports(checkout, [])
    if not os.path.exists(os.path.join(checkout, "shared.txt")):
        write_shared(checkout, {})
    if not os.path.exists(os.path.join(checkout, "README.md")):
        write_readme(checkout, read_ports(checkout))


def cmd_publish(checkout, zips):
    files = os.path.join(checkout, "files")
    ports = dict(read_ports(checkout))
    shared = read_shared(checkout)
    launcher = framework_launcher()
    for zpath in zips:
        with zipfile.ZipFile(zpath) as z:
            names = [n for n in z.namelist() if not n.endswith("/")]
            cores = [m.group(1) for n in names for m in [re.fullmatch(r"_Other/dnk/dnk-(.+)_\d{8}\.rbf", n)] if m]
            if len(cores) != 1:
                die("%s: not a release zip of one hybrid port (cores: %s)" % (zpath, cores or "none"))
            engine = cores[0]
            if "games/dnk/%s/games.txt" % engine not in names:
                die("%s: no games/dnk/%s/games.txt: package it again with the current hybrid/" % (zpath, engine))
            n = "games/dnk/%s/danik_hybrid_ports.sh" % engine
            if n not in names:
                die("%s: no %s" % (zpath, n))
            theirs = z.read(n)
            if launcher_version(theirs) > launcher_version(launcher):
                die("%s: its launcher is newer than this Hybrid_MiSTer's: update this checkout" % zpath)
            if launcher_version(theirs) == launcher_version(launcher) and theirs != launcher:
                die("%s: its launcher is version %d but not this Hybrid_MiSTer's: package it again with the current hybrid/"
                    % (zpath, launcher_version(theirs)))
            for n in names:
                ok = (n in NOT_SHIPPED or n in (LAUNCHER, BRIDGE) or n.startswith(LAUNCHER_DIR + "/")
                      or n.startswith("games/dnk/%s/" % engine)
                      or re.fullmatch(r"_Other/dnk/dnk-%s_\d{8}\.rbf" % re.escape(engine), n)
                      or re.fullmatch(r"_Other/dnk-[^/]+\.mgl", n)
                      or (n.startswith(SHARED_DIR + "/") and n not in PLAYERS))
                if ok and n.endswith(".mgl"):
                    ok = mgl_target(z.read(n))[0] == engine
                if not ok or n == CATALOGUE:
                    die("%s: %s is no file of %s" % (zpath, n, engine))
            # a shared file: the same bytes as the other ports that bring it
            for n in names:
                if n.startswith(SHARED_DIR + "/"):
                    others = [e for e in shared.get(n, []) if e != engine]
                    if not others:
                        continue
                    with open(os.path.join(files, n), "rb") as f:
                        same = f.read() == z.read(n)
                    if not same:
                        die("%s: its %s is not the one %s brings: package them with the same hybrid/"
                            % (zpath, n, ", ".join(others)))
            # the port's files of before go, the zip's come
            remove_paths(files, engine_paths(files, engine))
            drop_shared(checkout, shared, engine)
            for n in names:
                if n.startswith(SHARED_DIR + "/"):
                    shared.setdefault(n, []).append(engine)
                if n in NOT_SHIPPED or n == BRIDGE or n.startswith("Scripts/"):
                    continue
                dest = os.path.join(files, n)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(dest, "wb") as f:
                    f.write(z.read(n))
                mode = (z.getinfo(n).external_attr >> 16) & 0o777
                os.chmod(dest, mode or 0o644)
            ports[engine] = os.path.basename(zpath)
            print("%s: %s" % (engine, os.path.basename(zpath)))
    put_launcher(files)
    write_ports(checkout, list(ports.items()))
    write_shared(checkout, shared)
    write_catalogue(files, list(ports.items()))
    write_readme(checkout, list(ports.items()))
    commit(checkout, "Publish " + (", ".join(os.path.basename(z) for z in zips) or
                                   "the launcher, version %d" % launcher_version(launcher)))


def cmd_remove(checkout, engines):
    files = os.path.join(checkout, "files")
    ports = dict(read_ports(checkout))
    shared = read_shared(checkout)
    for engine in engines:
        if engine not in ports:
            die("%s is not in ports.txt" % engine)
        remove_paths(files, engine_paths(files, engine))
        drop_shared(checkout, shared, engine)
        del ports[engine]
    put_launcher(files)
    write_ports(checkout, list(ports.items()))
    write_shared(checkout, shared)
    write_catalogue(files, list(ports.items()))
    write_readme(checkout, list(ports.items()))
    commit(checkout, "Remove " + ", ".join(engines))


def main():
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == "init":
        cmd_init(a[1])
    elif len(a) >= 2 and a[0] == "publish":
        cmd_publish(a[1], a[2:])
    elif len(a) >= 3 and a[0] == "remove":
        cmd_remove(a[1], a[2:])
    elif len(a) == 2 and a[0] == "check":
        tags = check(a[1])
        print("ok: %d files, ports: %s" % (len(tags), ", ".join(e for e, _ in read_ports(a[1]))))
    elif len(a) == 4 and a[0] == "db":
        db = make_db(a[1], a[2], a[3])
        print("%s: %d files, %d folders" % (a[3], len(db["files"]), len(db["folders"])))
    elif len(a) == 2 and a[0] == "push":
        if not git(a[1], "remote").strip():
            git(a[1], "remote", "add", "origin", "https://github.com/%s.git" % REPO)
        git(a[1], "push", "origin", "main")
        git(a[1], "push", "-f", "origin", "db")
    else:
        die("usage: see the top of this file")


main()
