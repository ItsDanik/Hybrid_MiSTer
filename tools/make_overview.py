#!/usr/bin/env python3
# Make the table of the published ports in OVERVIEW.md, the page sent to
# whoever asks what the hybrid ports are: a row for each port of the
# database's ports.txt, with its name and games, the host version its release
# was built on, its first game's logo and its two screenshots. Run it after
# every publish (distribution.py publish) and whenever the host version
# changes; the rest of OVERVIEW.md (what the framework does, the credits) is
# written by hand.
#
# Each port's release zip is looked for in the core repositories (`releases/`,
# or `dist/` for a port without zips on GitHub, ScummVM):
# its games are those of the zip's games.txt; the host version is the VERSION
# of hybrid_host.sv in the hybrid/ the repository had when the zip was
# committed; the logo is package/logos/<id>.png of the first game, as it is; the screenshots are docs/screenshot1.png and screenshot2.png.
# The pictures are copied into docs/overview/, so the page needs nothing from
# the other repositories.
#
# usage: make_overview.py <database checkout> [<core repository>...]
#   without core repositories, every MiSTer-* next to the database checkout
import glob
import html
import os
import re
import shutil
import subprocess
import sys
import zipfile

PORT_W, VERSION_W = 190, 90      # the columns' widths, in pixels of the page
LOGO_W, SHOT_W = 176, 176         # the pictures'
START, END = "<!-- ports: tools/make_overview.py -->", "<!-- end of ports -->"

root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
out_dir = os.path.join(root, "docs", "overview")


def die(text):
    sys.exit("make_overview.py: " + text)


def git(repo, *args):
    return subprocess.run(["git", "-C", repo] + list(args), check=True, capture_output=True, text=True).stdout.strip()


def host_version(rev):
    """VERSION of hybrid_host.sv in this repository at `rev`"""
    try:
        text = git(root, "show", rev + ":rtl/hybrid_host.sv")
    except subprocess.CalledProcessError:
        die("hybrid %s is not in %s: fetch it" % (rev, root))
    return int(re.search(r"VERSION\s*=\s*32'd(\d+)", text).group(1))


def built_on(repo, zname):
    """The host version of the release: hybrid/ as the repository had it when
    the zip was committed; a port without zips on GitHub (ScummVM, too big
    for it: its zip in dist/), hybrid/ as the repository has it now"""
    if not os.path.isfile(os.path.join(repo, "releases", zname)):
        return host_version(git(repo, "rev-parse", "HEAD:hybrid"))
    commit = git(repo, "log", "-1", "--format=%H", "--", "releases/" + zname)
    if not commit:
        die("%s/releases/%s is not committed: release it first" % (repo, zname))
    return host_version(git(repo, "rev-parse", commit + ":hybrid"))


def web_url(repo):
    url = git(repo, "remote", "get-url", "origin")
    url = re.sub(r"^git@github\.com:", "https://github.com/", url)
    return re.sub(r"\.git$", "", url)


if len(sys.argv) < 2:
    die("usage: make_overview.py <database checkout> [<core repository>...]")
checkout = sys.argv[1]
repos = sys.argv[2:] or sorted(glob.glob(os.path.join(os.path.dirname(os.path.abspath(checkout)), "MiSTer-*")))

ports = []
with open(os.path.join(checkout, "ports.txt"), encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if line and not line.startswith("#"):
            ports.append(line.split("|"))

os.makedirs(out_dir, exist_ok=True)
rows, kept = [], {"banner.png"}
for engine, zname in ports:
    zpath = next((os.path.join(r, d, zname) for d in ("releases", "dist") for r in repos
                  if os.path.isfile(os.path.join(r, d, zname))), None)
    if zpath is None:
        die("no core repository has releases/%s or dist/%s (give the repositories as arguments)" % (zname, zname))
    repo = os.path.dirname(os.path.dirname(zpath))
    with zipfile.ZipFile(zpath) as z:
        games = [l.split("|", 1) for l in z.read("games/dnk/%s/games.txt" % engine).decode().splitlines()
                 if l.strip() and not l.startswith("#")]
    first = games[0][0]
    names = {"logo": engine + "_logo.png", "1": engine + "_1.png", "2": engine + "_2.png"}
    shutil.copyfile(os.path.join(repo, "package", "logos", first + ".png"), os.path.join(out_dir, names["logo"]))
    for n in ("1", "2"):
        src = os.path.join(repo, "docs", "screenshot%s.png" % n)
        if not os.path.isfile(src):
            die("%s has no docs/screenshot%s.png" % (repo, n))
        shutil.copyfile(src, os.path.join(out_dir, names[n]))
    kept.update(names.values())
    title = html.escape(games[0][1].strip())
    version = built_on(repo, zname)
    rows.append(
        '<tr>\n<td width="%d"><a href="%s"><b>%s</b></a><br><sub>%s</sub></td>\n<td width="%d" align="center">v%d</td>\n'
        '<td width="%d" align="center"><img src="docs/overview/%s" alt="%s" width="%d"></td>\n'
        '<td width="%d" align="center"><img src="docs/overview/%s" alt="%s, the title screen" width="%d">'
        ' <img src="docs/overview/%s" alt="%s, playing" width="%d"></td>\n</tr>\n'
        % (PORT_W, web_url(repo), engine, ", ".join(html.escape(t.strip()) for _, t in games), VERSION_W, version,
           LOGO_W + 16, names["logo"], title, LOGO_W,
           2 * SHOT_W + 24, names["1"], title, SHOT_W, names["2"], title, SHOT_W))
    print("%s: %s, %d games, host version %d" % (engine, zname, len(games), version))

for name in os.listdir(out_dir):
    if name not in kept:
        os.remove(os.path.join(out_dir, name))

# an HTML table, so that the columns keep their widths: in a Markdown one the
# games' names take the room and the pictures shrink
table = ('<table>\n<tr><th align="left">Port</th><th>Framework</th><th>Logo</th><th>Screenshots</th></tr>\n'
         + "".join(rows) + "</table>\n")
path = os.path.join(root, "OVERVIEW.md")
with open(path, encoding="utf-8") as f:
    page = f.read()
if START not in page or END not in page:
    die("OVERVIEW.md has no %s ... %s" % (START, END))
head, rest = page.split(START, 1)
page = head + START + "\n" + table + END + rest.split(END, 1)[1]
with open(path, "w", encoding="utf-8") as f:
    f.write(page)
print("OVERVIEW.md: %d ports, pictures in docs/overview/" % len(rows))
