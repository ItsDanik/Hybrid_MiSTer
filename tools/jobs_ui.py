"""jobs.py -i: the jobs of this PC in the look of mister-repos (Textual). The
table on the left, the details of the row picked on the right, the keys in
the footer; refreshed every few seconds in the background.

If the MiSTer answers at $MISTER (root@192.168.1.205), every row shows the
version of its port installed there (the date of _Other/dnk/dnk-<Engine>_
<date>.rbf), and the Hybrid_MiSTer rows the launcher's. One ssh, read-only:
at the start, every 5 minutes and on m.
"""
import glob, os, re, subprocess, threading, time

from rich.console import Group
from rich.table import Table
from rich.text import Text
from textual import work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, VerticalScroll
from textual.screen import ModalScreen
from textual.widgets import DataTable, Footer, Header, Static

import jobs

HOME = os.path.expanduser("~")
MISTER = os.environ.get("MISTER") or "root@192.168.1.205"
MISTER_EVERY = 300
COLUMNS = [("State", "state"), ("Kind", "kind"), ("Repo", "repo"), ("Name", "name"), ("Stage", "stage"),
           ("Time", "time"), ("Typical", "typical"), ("CPU", "cpu"), ("Mem", "mem"), ("When", "when"),
           ("Session", "session"), ("MiSTer", "mister")]


def ago(ts):
    s = max(0, time.time() - ts)
    for size, unit in ((86400 * 7, "w"), (86400, "d"), (3600, "h"), (60, "m")):
        if s >= size:
            return "%d%s ago" % (s // size, unit)
    return "just now"


def short_name(repo):
    name = os.path.basename(repo)
    return name[7:] if name.startswith("MiSTer-") else name


# --- the ports and the MiSTer

_engines = {}


def engine(repo):
    """What a repository installs on the MiSTer: its core's name (core/*.qpf),
    "launcher" for Hybrid_MiSTer, None for anything else"""
    if not repo:
        return None
    if repo not in _engines:
        qpf = glob.glob(os.path.join(repo, "core", "*.qpf"))
        if os.path.isfile(os.path.join(repo, "launcher", "danik_hybrid_ports.sh")):
            _engines[repo] = "launcher"
        else:
            _engines[repo] = os.path.splitext(os.path.basename(qpf[0]))[0] if qpf else None
    return _engines[repo]


def local_version(repo, name):
    """The repository's newest release (its date), or the launcher's VERSION"""
    if name == "launcher":
        try:
            with open(os.path.join(repo, "launcher", "danik_hybrid_ports.sh")) as f:
                m = re.search(r"^VERSION=(\S+)", f.read(), re.M)
            return m[1] if m else None
        except OSError:
            return None
    zips = sorted(glob.glob(os.path.join(repo, "releases", name + "_*.zip")))
    m = re.search(r"_(\d{8})\.zip$", zips[-1]) if zips else None
    return m[1] if m else None


def check_mister():
    """What the MiSTer has: the cores of the ports (also test copies and the
    old layout), the launcher's VERSION, the set name loaded"""
    names = sorted({os.path.splitext(os.path.basename(q))[0]
                    for q in glob.glob(os.path.join(HOME, "MiSTer*", "core", "*.qpf"))})
    old = " ".join("_Other/%s_*.rbf" % n for n in names if re.fullmatch(r"[\w-]+", n))
    script = """cd /media/fat || exit 3
for f in _Other/dnk/dnk-*_*.rbf _Other/.*_test/dnk-*_*.rbf %s; do
  [ -f "$f" ] && echo "R $(stat -c %%Y "$f") $f"
done
echo "L $(sed -n 's/^VERSION=//p' Scripts/danik_hybrid_ports.sh 2>/dev/null | head -n 1)"
echo "C $(cat /tmp/CORENAME 2>/dev/null)"
true""" % old
    try:
        r = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=4", "-o", "LogLevel=ERROR",
                            MISTER, script], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as e:
        return dict(ts=time.time(), found=False, error="timed out" if isinstance(e, subprocess.TimeoutExpired) else str(e))
    if r.returncode != 0:
        return dict(ts=time.time(), found=False,
                    error=(r.stderr.strip() or "ssh exited %d" % r.returncode).splitlines()[-1])
    res = dict(ts=time.time(), found=True, cores={}, launcher="", loaded="")
    for line in r.stdout.splitlines():
        kind, rest = line[:1], line[2:]
        if kind == "R":
            mtime, path = rest.split(" ", 1)
            m = re.match(r"(?:dnk-)?(.+)_(\d{8})\.rbf$", os.path.basename(path))
            if m:
                res["cores"].setdefault(m[1], []).append(
                    dict(date=m[2], path=path, mtime=int(mtime), test="_test/" in path))
        elif kind == "L":
            res["launcher"] = rest.strip()
        elif kind == "C":
            res["loaded"] = rest.strip()
    for found in res["cores"].values():
        found.sort(key=lambda c: (c["test"], c["date"]))
    return res


def installed(mister, name):
    """The installed core of a port (the newest, not a test copy) and its test copy"""
    cores = mister["cores"].get(name, [])
    return (next((c for c in reversed(cores) if not c["test"]), None),
            next((c for c in cores if c["test"]), None))


def loaded(mister, name):
    n = mister.get("loaded", "")
    return n == name or n.startswith(name + "_")


def mister_cell(repo, mister):
    name = engine(repo)
    if not name or mister is None:
        return Text("…" if name else "", style="dim")
    if not mister["found"]:
        return Text("")
    mine = local_version(repo, name)
    if name == "launcher":
        have = mister["launcher"]
        if not have:
            return Text("—", style="dim")
        style = "green" if have == mine else "cyan" if mine and have.isdigit() and mine.isdigit() \
            and int(have) > int(mine) else "yellow"
        return Text("launcher " + have, style=style)
    core, test = installed(mister, name)
    if not core:
        return Text("test" if test else "—", style="magenta" if test else "dim")
    style = "" if not mine else "green" if core["date"] == mine else "cyan" if core["date"] > mine else "yellow"
    cell = Text(core["date"], style=style)
    if test:
        cell.append(" +test", style="magenta")
    if loaded(mister, name):
        cell.append(" ▶", style="bold green")
    return cell


# --- the table and the details

def repo_of(item):
    if item["what"] == "job":
        return item["job"]["repo"]
    if item["what"] == "container":
        return item["container"]["repo"]
    if item["what"] == "ended":
        return item["ended"]["repo"]
    return ""


def row_cells(item, vals, mister):
    """The cells of a row: vals are the row's values of jobs.collect() by column"""
    what = item["what"]
    dim = lambda s: Text(str(s), style="dim")
    repo = repo_of(item)
    if what == "job" and item["job"].get("started"):
        state = Text("running", style="bold green")
        when = time.strftime("%H:%M", time.localtime(int(item["job"]["started"])))
    elif what == "job":
        state = Text("waiting", style="yellow")
        when = time.strftime("%H:%M", time.localtime(int(item["job"]["queued"])))
    elif what == "container":
        c = item["container"]
        state = Text("orphan", style="bold red") if c["job"] else Text("no slot", style="yellow")
        when = time.strftime("%H:%M", time.localtime(c["started"]))
    elif what == "program":
        p = item["program"]
        state = Text("orphan", style="bold red") if p["orphan"] else Text("program", style="cyan")
        when = time.strftime("%H:%M", time.localtime(time.time() - p["age"]))
    else:
        r = item["ended"]
        state = Text("ok", style="green") if r["rc"] == 0 else Text("exit %d" % r["rc"], style="red")
        when = vals.get("ENDED", "")
    if what == "program":
        p = item["program"]
        return [state, Text("program"), dim(os.path.basename(p["cwd"])), Text(p["args"].split(" ")[0].rsplit("/", 1)[-1]),
                dim(p["args"][:30]), Text(vals.get("AGE", "")), "", "", "", dim(when), dim(p["session"]), Text("")]
    stage = vals.get("STAGE") or vals.get("RESULT", "")
    return [state, Text(vals.get("KIND", "")), Text(short_name(repo) if repo else ""), Text(vals.get("NAME", "")),
            Text(stage, style="cyan" if what != "ended" else "dim"),
            Text(vals.get("ELAPSED") or vals.get("WAITING") or vals.get("TOOK", "")),
            dim(vals.get("TYPICAL", "")), Text(vals.get("CPU", "")), Text(vals.get("MEM", "")), dim(when),
            dim(vals.get("SESSION", "")), mister_cell(repo, mister)]


def section(title):
    return Text("\n" + title, style="bold underline")


def kv_table():
    t = Table.grid(padding=(0, 2))
    t.add_column(style="bold", no_wrap=True, min_width=10)
    t.add_column()
    return t


def render_mister(repo, mister):
    out = [section("MiSTer  " + MISTER)]
    if mister is None:
        return out + [Text("checking…", style="dim")]
    if not mister["found"]:
        return out + [Text("not found: %s" % mister["error"], style="red"),
                      Text("m to look again", style="dim")]
    t = kv_table()
    t.add_row("Checked", ago(mister["ts"]))
    t.add_row("Loaded", mister["loaded"] or Text("—", style="dim"))
    name = engine(repo)
    mine = local_version(repo, name) if name else None
    if name == "launcher":
        have = mister["launcher"]
        t.add_row("Launcher", Text.assemble(have or "not installed", "  ",
                                            ("same as this repository" if have == mine else
                                             "this repository has %s" % mine, "green" if have == mine else "yellow")))
        width = max(map(len, mister["cores"]), default=0)
        for n in sorted(mister["cores"], key=str.lower):
            core, test = installed(mister, n)
            t.add_row("", Text.assemble((n.ljust(width), "bold"), "  ", core["date"] if core else ("—", "dim"),
                                        ("  test copy %s" % test["date"] if test else "", "magenta"),
                                        ("  ▶ loaded" if loaded(mister, n) else "", "green")))
    elif name:
        core, test = installed(mister, name)
        if core:
            note = ("same as releases/%s_%s.zip" % (name, mine), "green") if core["date"] == mine else \
                ("newer than releases/%s_%s.zip" % (name, mine), "cyan") if mine and core["date"] > mine else \
                ("releases/ has %s" % mine, "yellow") if mine else ("no release in releases/", "dim")
            t.add_row("Installed", Text.assemble(core["date"], "  ", (core["path"], "dim"),
                                                 "  %s  " % ago(core["mtime"]), note))
        else:
            t.add_row("Installed", Text("not installed", style="dim"))
        if test:
            t.add_row("Test copy", Text.assemble((test["date"], "magenta"), "  ", (test["path"], "dim"),
                                                 "  " + ago(test["mtime"])))
        if loaded(mister, name):
            t.add_row("", Text("▶ loaded now (%s)" % mister["loaded"], style="green"))
    out.append(t)
    return out


def render_detail(item, data, mister):
    if item is None:
        out = [Text("This PC", style="bold cyan"), Text(data["head"] if data else "reading…", style="dim"),
               Text("\nnothing runs, waits or has ended yet", style="dim")]
        return Group(*out, *render_mister(os.path.join(HOME, "Hybrid_MiSTer"), mister))
    repo = repo_of(item)
    out = [Text.assemble((jobs.label(item), "bold cyan"))]
    if repo:
        out.append(Text(repo.replace(HOME, "~", 1), style="dim"))
    out.append(section({"job": "Job", "container": "Container", "program": "Program",
                        "ended": "Ended"}[item["what"]]))
    t = kv_table()
    for k, v in jobs.details(item):
        t.add_row(k[:1].upper() + k[1:], Text(str(v)))
    out.append(t)
    if engine(repo):
        out += render_mister(repo, mister)
    return Group(*out)


# --- the app

class Confirm(ModalScreen):
    CSS = """
    Confirm { align: center middle; }
    #box { width: auto; max-width: 80%; height: auto; border: thick $error; background: $panel; padding: 1 2; }
    """
    BINDINGS = [Binding("y", "yes", "End it"), Binding("n,escape", "no", "Keep it")]

    def __init__(self, item):
        super().__init__()
        self.item = item

    def compose(self) -> ComposeResult:
        yield Static(id="box", content=Text.assemble("End ", (jobs.label(self.item), "bold"), "?\n\n",
                                       ("y", "bold"), " end it    ", ("n", "bold"), " keep it"), expand=False)

    def action_yes(self):
        self.dismiss(True)

    def action_no(self):
        self.dismiss(False)


class JobsApp(App):
    TITLE = "Jobs on this PC"
    CSS = """
    #bar { height: auto; padding: 0 1; background: $panel; }
    #table { width: auto; max-width: 70%; }
    #detail-scroll { width: 1fr; border-left: solid $primary; padding: 0 1; }
    """
    BINDINGS = [
        Binding("k,delete", "end", "End"),
        Binding("h", "history", "History"),
        Binding("r", "refresh", "Refresh"),
        Binding("m", "mister", "MiSTer"),
        Binding("q", "quit", "Quit"),
    ]

    def __init__(self, every=3):
        super().__init__()
        self.every = every
        self.ended = 10
        self.data = None
        self.items = {}
        self.keys = []
        self.mister = None
        self.wake = threading.Event()

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static(id="bar")
        with Horizontal():
            yield DataTable(id="table", cursor_type="row", zebra_stripes=True)
            with VerticalScroll(id="detail-scroll"):
                yield Static(id="detail")
        yield Footer()

    def on_mount(self):
        table = self.query_one(DataTable)
        for label, key in COLUMNS:
            table.add_column(label, key=key)
        self.sub_title = "last %d ended" % self.ended
        self.show_bar()
        self.show_detail()
        threading.Thread(target=self.collector, daemon=True).start()
        self.mister_check()
        self.set_interval(MISTER_EVERY, self.mister_check)
        table.focus()

    # --- background work

    def collector(self):
        while True:
            try:
                data, error = jobs.collect(self.ended), None
            except Exception as e:    # the screen goes on with what it had
                data, error = None, "could not read everything: %s" % e
            try:
                self.call_from_thread(self.apply, data, error)
            except RuntimeError:    # the app has ended
                return
            self.wake.wait(self.every)
            self.wake.clear()

    @work(thread=True, group="mister", exclusive=True)
    def mister_check(self):
        self.mister = check_mister()
        self.call_from_thread(self.mister_changed)

    @work(thread=True)
    def end_item(self, item):
        msg = jobs.end(item)
        self.wake.set()
        self.call_from_thread(self.notify, msg, severity="information" if msg.startswith("ended") else "warning")

    # --- the screen

    def apply(self, data, error):
        if error:
            self.notify(error, severity="error")
            return
        self.data = data
        rows = [(r[1], dict(zip(sec["columns"], (str(v) for v in r[0]))))
                for sec in data["sections"] for r in sec["rows"]]
        self.items = {item["key"]: (item, vals) for item, vals in rows}
        self.show_rows()
        self.show_bar()

    def show_rows(self):
        table = self.query_one(DataTable)
        keys = list(self.items)
        if keys == self.keys:
            for key in keys:
                item, vals = self.items[key]
                for (_, col), value in zip(COLUMNS, row_cells(item, vals, self.mister)):
                    table.update_cell(key, col, value, update_width=True)
        else:
            current = self.selected()
            table.clear()
            for key in keys:
                item, vals = self.items[key]
                table.add_row(*row_cells(item, vals, self.mister), key=key)
            self.keys = keys
            if current and current["key"] in self.items:
                table.move_cursor(row=table.get_row_index(current["key"]), animate=False)
        self.show_detail()

    def mister_changed(self):
        self.show_bar()
        if self.data:
            self.show_rows()
        else:
            self.show_detail()

    def show_bar(self):
        m = self.mister
        line = Text.assemble(("MiSTer ", "bold"), MISTER, "  ")
        if m is None:
            line.append("looking…", style="dim")
        elif not m["found"]:
            line.append("not found", style="red")
            line.append("  %s, %s" % (m["error"], ago(m["ts"])), style="dim")
        else:
            line.append("launcher %s" % (m["launcher"] or "—"), style="green")
            line.append("  ·  %d ports" % len([n for n in m["cores"] if installed(m, n)[0]]))
            line.append("  ·  loaded %s" % (m["loaded"] or "—"))
            line.append("  ·  checked %s" % ago(m["ts"]), style="dim")
        head = Text(self.data["head"]) if self.data else Text("reading docker…", style="dim")
        self.query_one("#bar", Static).update(Group(head, line))

    def selected(self):
        table = self.query_one(DataTable)
        if not table.row_count:
            return None
        key = table.coordinate_to_cell_key(table.cursor_coordinate).row_key.value
        return self.items.get(key, (None,))[0]

    def show_detail(self):
        self.query_one("#detail", Static).update(render_detail(self.selected(), self.data, self.mister))

    def on_data_table_row_highlighted(self, event):
        self.show_detail()
        self.query_one("#detail-scroll").scroll_home(animate=False)

    # --- actions

    def action_end(self):
        item = self.selected()
        if item is None:
            return
        if item["what"] == "ended":
            self.notify("an ended job cannot be ended", severity="warning")
            return

        def answer(yes):
            if yes:
                self.end_item(item)
            else:
                self.notify("nothing ended")
        self.push_screen(Confirm(item), answer)

    def action_history(self):
        self.ended = 10 if self.ended is None else None
        self.sub_title = "last %d ended" % self.ended if self.ended else "all ended"
        self.wake.set()

    def action_refresh(self):
        self.wake.set()

    def action_mister(self):
        self.notify("looking for the MiSTer at %s…" % MISTER)
        self.mister_check()


def run(every=3):
    JobsApp(every).run()
