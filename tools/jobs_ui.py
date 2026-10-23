"""jobs.py -i: the jobs of this PC in the look of mister-repos (Textual). The
table on the left, the details of the row picked on the right, the keys in
the footer; refreshed every few seconds in the background.
"""
import os, threading, time

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
COLUMNS = [("State", "state"), ("Kind", "kind"), ("Repo", "repo"), ("Name", "name"), ("Stage", "stage"),
           ("Time", "time"), ("Typical", "typical"), ("CPU", "cpu"), ("Mem", "mem"), ("When", "when"),
           ("Session", "session")]


def short_name(repo):
    name = os.path.basename(repo)
    return name[7:] if name.startswith("MiSTer-") else name


def repo_of(item):
    if item["what"] == "job":
        return item["job"]["repo"]
    if item["what"] == "container":
        return item["container"]["repo"]
    if item["what"] == "ended":
        return item["ended"]["repo"]
    return ""


def row_cells(item, vals):
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
                dim(p["args"][:30]), Text(vals.get("AGE", "")), "", "", "", dim(when), dim(p["session"])]
    stage = vals.get("STAGE") or vals.get("RESULT", "")
    return [state, Text(vals.get("KIND", "")), Text(short_name(repo) if repo else ""), Text(vals.get("NAME", "")),
            Text(stage, style="cyan" if what != "ended" else "dim"),
            Text(vals.get("ELAPSED") or vals.get("WAITING") or vals.get("TOOK", "")),
            dim(vals.get("TYPICAL", "")), Text(vals.get("CPU", "")), Text(vals.get("MEM", "")), dim(when),
            dim(vals.get("SESSION", ""))]


def section(title):
    return Text("\n" + title, style="bold underline")


def kv_table():
    t = Table.grid(padding=(0, 2))
    t.add_column(style="bold", no_wrap=True, min_width=10)
    t.add_column()
    return t


def render_detail(item, data):
    if item is None:
        return Group(Text("This PC", style="bold cyan"), Text(data["head"] if data else "reading…", style="dim"),
                     Text("\nnothing runs, waits or has ended yet", style="dim"))
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
    return Group(*out)


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
                                                     ("y", "bold"), " end it    ", ("n", "bold"), " keep it"),
                     expand=False)

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
        Binding("q", "quit", "Quit"),
    ]

    def __init__(self, every=3):
        super().__init__()
        self.every = every
        self.ended = 10
        self.data = None
        self.items = {}
        self.keys = []
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
                for (_, col), value in zip(COLUMNS, row_cells(item, vals)):
                    table.update_cell(key, col, value, update_width=True)
        else:
            current = self.selected()
            table.clear()
            for key in keys:
                item, vals = self.items[key]
                table.add_row(*row_cells(item, vals), key=key)
            self.keys = keys
            if current and current["key"] in self.items:
                table.move_cursor(row=table.get_row_index(current["key"]), animate=False)
        self.show_detail()

    def show_bar(self):
        self.query_one("#bar", Static).update(
            Text(self.data["head"]) if self.data else Text("reading docker…", style="dim"))

    def selected(self):
        table = self.query_one(DataTable)
        if not table.row_count:
            return None
        key = table.coordinate_to_cell_key(table.cursor_coordinate).row_key.value
        return self.items.get(key, (None,))[0]

    def show_detail(self):
        self.query_one("#detail", Static).update(render_detail(self.selected(), self.data))

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


def run(every=3):
    JobsApp(every).run()
