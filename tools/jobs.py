#!/usr/bin/env python3
"""What runs on this PC: the Quartus builds and simulations started through
tools/job.sh (running and waiting for a slot), other containers of the hybrid
images (cores whose build_core.sh predates job.sh, toolchain builds),
programs of the framework running outside any container (fakecore, a game
against it), and the jobs that ended.

usage: tools/jobs.py                  the table once
       tools/jobs.py -i [seconds]     interactive, refreshed every few
                                      seconds (3): pick a row with the
                                      arrows, Enter for its details, k to
                                      end it, h for the whole history
       tools/jobs.py watch [seconds]  again every few seconds (5)
       tools/jobs.py history [n]      the last n jobs that ended (20)
       tools/jobs.py kill <what>      end a job (its id or its repository's
                                      name), a container (its id) or a
                                      program (its pid)
       tools/jobs.py status           one short line for a status line
                                      (no docker; empty when nothing runs)
"""
import calendar, json, os, re, signal, statistics, subprocess, sys, threading, time

DIR = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"), "dnk-jobs")
SLOTS = {"quartus": 3, "sim": 4}
IMAGES = {"theypsilon/quartus-lite-c5": "quartus", "mister-hybrid-sim": "sim", "mister-hybrid-tc": "build"}
STAGES = {"quartus_map": "synthesis", "quartus_fit": "fit", "quartus_asm": "assembler",
          "quartus_sta": "timing", "quartus_cdb": "netlist", "quartus_eda": "netlist"}
QUIET = {"sh", "bash", "dash", "docker-init", "tini", "make", "quartus_sh", "timeout", "tee", "sleep"}


def duration(s):
    s = int(s)
    if s >= 3600:
        return "%d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)
    return "%d:%02d" % (s // 60, s % 60)


def minutes(s):
    return "~%dm" % max(1, round(s / 60))


def alive(pid):
    try:
        os.kill(int(pid), 0)
        return True
    except (OSError, ValueError):
        return False


def run(*args):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.TimeoutExpired):
        return ""


# --- job.sh's files

def read_jobs(state):
    jobs = []
    folder = os.path.join(DIR, state)
    for name in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        try:
            with open(os.path.join(folder, name)) as f:
                job = dict(l.rstrip("\n").split("=", 1) for l in f if "=" in l)
        except OSError:
            continue
        if not alive(job.get("pid")):
            try:    # job.sh was killed without its trap (SIGKILL); it is no longer in the way
                os.remove(os.path.join(folder, name))
            except OSError:
                pass
            continue
        job["id"] = name
        jobs.append(job)
    return jobs


def read_history():
    rows = []
    try:
        with open(os.path.join(DIR, "history.tsv")) as f:
            for line in f:
                p = line.rstrip("\n").split("\t")
                if len(p) >= 10:
                    rows.append(dict(end=int(p[0]), started=int(p[1]), queued=int(p[2]), seconds=int(p[3]),
                                     rc=int(p[4]), kind=p[5], name=p[6], repo=p[7], session=p[8], result=p[9]))
    except OSError:
        pass
    return rows


def flow_elapsed(repo, name):
    """A Quartus build's time from the last flow report, where no history has it"""
    try:
        with open(os.path.join(repo, "core", "output_files", name + ".flow.rpt"), errors="replace") as f:
            text = f.read()
    except OSError:
        return None
    m = re.search(r"Flow Elapsed Time.*?^; Total\s*;\s*(\d+):(\d+):(\d+)", text, re.S | re.M)
    return int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3]) if m else None


def typical(history, kind, name, repo):
    """The median of the last five runs that ended well, and where it is from"""
    same = [r["seconds"] for r in history if r["rc"] == 0 and r["kind"] == kind and r["name"] == name
            and os.path.basename(r["repo"]) == os.path.basename(repo)][-5:]
    if same:
        return statistics.median(same)
    if kind == "quartus" and repo:
        return flow_elapsed(repo, name)
    return None


# --- docker

def repo_of(path):
    """The repository a folder belongs to: the core around hybrid/, as job.sh has it"""
    out = run("git", "-C", path, "rev-parse", "--show-superproject-working-tree").strip()
    return out or run("git", "-C", path, "rev-parse", "--show-toplevel").strip() or path


def containers():
    ids = run("docker", "ps", "-q").split()
    if not ids:
        return []
    try:
        info = json.loads(run("docker", "inspect", *ids) or "[]")
    except ValueError:
        return []
    found = []
    for c in info:
        image = c["Config"]["Image"].split(":")[0]
        labels = c["Config"].get("Labels") or {}
        kind = IMAGES.get(image)
        if not kind and "dnk.job" not in labels:
            continue
        mounts = [m["Source"] for m in c.get("Mounts", []) if m.get("Type") == "bind" and m.get("RW", True)]
        cmd = " ".join(c["Config"].get("Cmd") or [])
        m = re.search(r"--flow compile (\S+)", cmd)
        started = c["State"]["StartedAt"]
        found.append(dict(id=c["Id"][:12], kind=kind or "other", job=labels.get("dnk.job"),
                          repo=repo_of(mounts[0]) if mounts else "", name=m[1] if m else "",
                          started=calendar.timegm(time.strptime(started[:19], "%Y-%m-%dT%H:%M:%S")),
                          image=c["Config"]["Image"], command=cmd, cpu="", mem="", stage=""))
    if found:
        for line in run("docker", "stats", "--no-stream", "--format", "{{json .}}", *[c["id"] for c in found]).splitlines():
            try:
                s = json.loads(line)
            except ValueError:
                continue
            for c in found:
                if s.get("ID", "").startswith(c["id"]):
                    c["cpu"] = s.get("CPUPerc", "")
                    c["mem"] = s.get("MemUsage", "").split(" / ")[0]
        for c in found:
            names = [l.split(None, 1)[1].strip() for l in run("docker", "top", c["id"], "-o", "pid,comm").splitlines()[1:]
                     if len(l.split(None, 1)) == 2]
            stage = [STAGES.get(n, n) for n in names if n not in QUIET]
            c["stage"] = stage[-1] if stage else (names[-1] if names else "")
    return found


# --- programs outside containers

def in_container(pid):
    try:
        with open("/proc/%d/cgroup" % pid) as f:
            return "docker" in f.read()
    except OSError:
        return True


def strays():
    """fakecore, and every program with MISTER_HYBRID_SHM (a game against fakecore)"""
    found = []
    me = os.getuid()
    boot = time.time() - float(open("/proc/uptime").read().split()[0])
    hz = os.sysconf("SC_CLK_TCK")
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        pid = int(d)
        try:
            if os.stat("/proc/" + d).st_uid != me:
                continue
            comm = open("/proc/%d/comm" % pid).read().strip()
            if comm != "fakecore":
                with open("/proc/%d/environ" % pid, "rb") as f:
                    if b"MISTER_HYBRID_SHM=" not in f.read():
                        continue
            if in_container(pid):
                continue
            stat = open("/proc/%d/stat" % pid).read().rsplit(")", 1)[1].split()
            ppid = int(stat[1])
            parent = open("/proc/%d/comm" % ppid).read().strip() if ppid > 1 else "init"
            args = open("/proc/%d/cmdline" % pid, "rb").read().replace(b"\0", b" ").decode(errors="replace").strip()
            cwd = os.readlink("/proc/%d/cwd" % pid)
        except OSError:
            continue
        m = re.search(r"/claude-\d+/[^/]+/([0-9a-f-]{8})[0-9a-f-]*/", cwd + "/")
        found.append(dict(pid=pid, age=time.time() - (boot + int(stat[19]) / hz), args=args, cwd=cwd,
                          parent=parent, orphan=parent in ("systemd", "init"), session=m[1] if m else ""))
    return found


# --- output

def table(rows, header):
    widths = [max(len(str(r[i])) for r in rows + [header]) for i in range(len(header))]
    lines = ["  ".join(str(v).ljust(w) for v, w in zip(header, widths)).rstrip()]
    lines += ["  ".join(str(v).ljust(w) for v, w in zip(r, widths)).rstrip() for r in rows]
    return lines


def slots(jobs):
    """The slots of each kind, as the jobs were started with (DNK_JOB_SLOTS)"""
    n = dict(SLOTS)
    for j in jobs:
        if j.get("slots", "").isdigit():
            n[j["kind"]] = int(j["slots"])
    return n




def when(t):
    return time.strftime("%a %d %b %H:%M:%S", time.localtime(int(t))) if t else "-"


def ended_rows(rows):
    return [([time.strftime("%a %H:%M", time.localtime(r["end"])), r["kind"], os.path.basename(r["repo"]),
              r["name"], duration(r["seconds"]), duration(r["queued"]) if r["queued"] else "",
              "ok" if r["rc"] == 0 else "exit %d" % r["rc"], r["result"], r["session"][:8]],
             dict(what="ended", key="ended %d %s %s" % (r["end"], r["repo"], r["name"]), ended=r,
                  tone="" if r["rc"] == 0 else "bad"))
            for r in reversed(rows)]


ENDED_COLUMNS = ["ENDED", "KIND", "REPO", "NAME", "TOOK", "WAITED", "", "RESULT", "SESSION"]


def collect(ended=6):
    """What jobs.py shows: a line about the PC, then the sections, each with
    a title, its columns, its rows (the values, and the job, container,
    program or ended job the row is) and a note. ended: how many of the jobs
    that ended, None for all."""
    now = time.time()
    history = read_history()
    running, waiting = read_jobs("running"), read_jobs("waiting")
    boxes = containers()

    load = os.getloadavg()
    mem = dict(l.split(":", 1) for l in open("/proc/meminfo"))
    avail = int(mem["MemAvailable"].split()[0]) / 1048576
    total = int(mem["MemTotal"].split()[0]) / 1048576
    counts = "  ".join("%s %d/%d%s" % (k, sum(j["kind"] == k for j in running), n,
                                        " +%d waiting" % w if (w := sum(j["kind"] == k for j in waiting)) else "")
                       for k, n in slots(running + waiting).items())
    head = ("%s   load %.1f %.1f %.1f on %d threads   memory %.1f of %.0f GB free   %s"
            % (time.strftime("%H:%M:%S"), *load, os.cpu_count(), avail, total, counts))

    rows = []
    for j in running:
        mine = [c for c in boxes if c["job"] == j["id"]]
        for c in mine:
            c["shown"] = True
        t = typical(history, j["kind"], j["name"], j["repo"])
        rows.append(([j["id"], j["kind"], os.path.basename(j["repo"]), j["name"],
                      ", ".join(c["stage"] for c in mine if c["stage"]) or "-",
                      duration(now - int(j["started"] or now)), minutes(t) if t else "",
                      " ".join(c["cpu"] for c in mine), " ".join(c["mem"] for c in mine), j["session"][:8]],
                     dict(what="job", key=j["id"], job=j, containers=mine, tone="")))
    for c in boxes:
        if c.get("shown"):
            continue
        t = typical(history, c["kind"], c["name"], c["repo"]) if c["name"] else None
        rows.append(([c["id"] + ("!" if c["job"] else "*"), c["kind"], os.path.basename(c["repo"]), c["name"],
                      c["stage"], duration(now - c["started"]), minutes(t) if t else "", c["cpu"], c["mem"], ""],
                     dict(what="container", key=c["id"], container=c, tone="bad" if c["job"] else "")))
    notes = []
    if any(r[1]["what"] == "container" and not r[1]["container"]["job"] for r in rows):
        notes.append("* a container not started through job.sh: it takes no slot")
    if any(r[1]["what"] == "container" and r[1]["container"]["job"] for r in rows):
        notes.append("! an orphan: its job.sh was killed (SIGKILL), it runs on without a slot")
    sections = [dict(title="RUNNING", rows=rows, note="\n".join(notes),
                     columns=["ID", "KIND", "REPO", "NAME", "STAGE", "ELAPSED", "TYPICAL", "CPU", "MEM", "SESSION"])]

    sections.append(dict(title="WAITING FOR A SLOT", note="",
                         columns=["ID", "KIND", "REPO", "NAME", "WAITING", "SESSION"],
                         rows=[([j["id"], j["kind"], os.path.basename(j["repo"]), j["name"],
                                 duration(now - int(j["queued"])), j["session"][:8]],
                                dict(what="job", key=j["id"], job=j, containers=[], tone="wait"))
                               for j in waiting]))

    progs = strays()
    sections.append(dict(title="PROGRAMS OUTSIDE CONTAINERS",
                         columns=["PID", "AGE", "", "SESSION", "COMMAND", "FOLDER"],
                         rows=[([p["pid"], duration(p["age"]), "orphan" if p["orphan"] else "", p["session"],
                                 p["args"][:60], p["cwd"]],
                                dict(what="program", key="pid %d" % p["pid"], program=p,
                                     tone="bad" if p["orphan"] else ""))
                               for p in progs],
                         note="orphan: what started it has gone; probably left over from a test"
                         if any(p["orphan"] for p in progs) else ""))

    shown = history if ended is None else history[-ended:] if ended else []
    sections.append(dict(title="ENDED" if ended is not None else "ENDED, ALL %d" % len(history),
                         columns=ENDED_COLUMNS, rows=ended_rows(shown), note=""))
    return dict(head=head, sections=sections)


def show():
    data = collect()
    out = [data["head"]]
    for sec in data["sections"]:
        if not sec["rows"]:
            if sec["title"] == "RUNNING":
                out += ["", "RUNNING  nothing"]
            continue
        out += ["", sec["title"]] + table([r[0] for r in sec["rows"]], sec["columns"])
        if sec["note"]:
            out += sec["note"].split("\n")
    return "\n".join(out)


def label(item):
    def parts(*p):
        return " ".join(x for x in p if x)
    if item["what"] == "job":
        j = item["job"]
        return "job %s (%s)" % (j["id"], parts(j["kind"], os.path.basename(j["repo"]), j["name"]))
    if item["what"] == "container":
        c = item["container"]
        return "container %s (%s)" % (c["id"], parts(c["kind"], os.path.basename(c["repo"]), c["name"]))
    if item["what"] == "program":
        p = item["program"]
        return "program %d (%s)" % (p["pid"], p["args"][:40])
    return "ended job %s %s" % (os.path.basename(item["ended"]["repo"]), item["ended"]["name"])


def end(item):
    """End a job (job.sh's trap ends its command and containers), a container
    or a program; what happened, in a line"""
    try:
        if item["what"] == "job":
            os.kill(int(item["job"]["pid"]), signal.SIGTERM)
        elif item["what"] == "container":
            run("docker", "kill", item["container"]["id"])
        elif item["what"] == "program":
            os.kill(item["program"]["pid"], signal.SIGTERM)
        else:
            return "%s is over already" % label(item)
    except ProcessLookupError:
        return "%s had ended already" % label(item)
    except OSError as e:
        return "could not end %s: %s" % (label(item), e)
    return "ended " + label(item)


def details(item):
    """The lines of a row's details page"""
    if item["what"] == "job":
        j = item["job"]
        lines = [("job", j["id"]), ("kind", j["kind"]), ("name", j["name"]), ("repository", j["repo"]),
                 ("command", j.get("command", "")), ("session", j["session"] or "-"), ("job.sh pid", j["pid"]),
                 ("queued", when(j["queued"])), ("started", when(j["started"]) if j["started"] else "waiting"),
                 ("slot", "%s of %s" % (j["slot"], j.get("slots", "?")) if j["started"] else "-")]
        lines += [("container", "%s %s: %s, CPU %s, %s" % (c["id"], c["image"], c["stage"], c["cpu"], c["mem"]))
                  for c in item["containers"]]
    elif item["what"] == "container":
        c = item["container"]
        lines = [("container", c["id"]), ("image", c["image"]), ("kind", c["kind"]), ("repository", c["repo"]),
                 ("name", c["name"] or "-"), ("command", c["command"]), ("started", when(c["started"])),
                 ("stage", c["stage"]), ("CPU", c["cpu"]), ("memory", c["mem"]),
                 ("", "an orphan of job %s, whose job.sh was killed: it takes no slot" % c["job"] if c["job"]
                  else "not started through job.sh: it takes no slot")]
    elif item["what"] == "program":
        p = item["program"]
        lines = [("pid", p["pid"]), ("command", p["args"]), ("folder", p["cwd"]), ("age", duration(p["age"])),
                 ("parent", "gone (orphan): probably left over from a test" if p["orphan"] else p["parent"]),
                 ("session", p["session"] or "-")]
    else:
        r = item["ended"]
        lines = [("ended", when(r["end"])), ("kind", r["kind"]), ("name", r["name"]), ("repository", r["repo"]),
                 ("started", when(r["started"])), ("took", duration(r["seconds"])),
                 ("waited", duration(r["queued"])), ("exit code", r["rc"]), ("result", r["result"] or "-"),
                 ("session", r["session"] or "-")]
    return ["%-11s %s" % (k, v) for k, v in lines]


def interactive(every):
    """The table refreshed in the background, its rows picked with the keys"""
    import curses, locale
    locale.setlocale(locale.LC_CTYPE, "")    # the arrows of the help line
    os.environ.setdefault("ESCDELAY", "25")
    state = dict(data=None, ended=6, wake=threading.Event(), error="")

    def collector():
        while True:
            try:
                state["data"] = collect(state["ended"])
                state["error"] = ""
            except Exception as e:    # the screen goes on with what it had
                state["error"] = "could not read everything: %s" % e
            state["wake"].wait(every)
            state["wake"].clear()

    threading.Thread(target=collector, daemon=True).start()
    try:
        curses.wrapper(screen, state)
    except KeyboardInterrupt:
        pass


def screen(scr, state):
    import curses
    try:
        curses.curs_set(0)
    except curses.error:    # a terminal that cannot hide it
        pass
    scr.timeout(200)
    tones = {"": 0}
    if curses.has_colors():
        curses.use_default_colors()
        for n, (tone, colour) in enumerate((("bad", curses.COLOR_RED), ("wait", curses.COLOR_YELLOW)), 1):
            curses.init_pair(n, colour, -1)
            tones[tone] = curses.color_pair(n)
    key, view, confirm, message, top = None, None, None, "", 0
    was = None    # the page shown last: a new one is drawn from a clear screen

    def put(y, text, attr=0, fill=False):
        h, w = scr.getmaxyx()
        if 0 <= y < h and w > 1:
            try:
                scr.addnstr(y, 0, text.ljust(w - 1) if fill else text, w - 1, attr)
            except curses.error:
                pass

    while True:
        data = state["data"]
        h, w = scr.getmaxyx()
        lines = []    # (text, attribute, the row's item)
        if data is None:
            lines.append(("reading docker...", 0, None))
        else:
            lines.append((data["head"], curses.A_BOLD, None))
            for sec in data["sections"]:
                if not sec["rows"] and sec["title"] != "RUNNING":
                    continue
                lines.append(("", 0, None))
                if not sec["rows"]:
                    lines.append(("RUNNING  nothing", curses.A_BOLD, None))
                    continue
                lines.append((sec["title"], curses.A_BOLD, None))
                text = table([r[0] for r in sec["rows"]], sec["columns"])
                lines.append((text[0], curses.A_DIM, None))
                for t, (_, item) in zip(text[1:], sec["rows"]):
                    lines.append((t, tones.get(item["tone"], 0), item))
                for note in sec["note"].split("\n") if sec["note"] else []:
                    lines.append((note, curses.A_DIM, None))
        picks = [n for n, l in enumerate(lines) if l[2]]
        keys = [lines[n][2]["key"] for n in picks]
        if key not in keys:
            key = keys[0] if keys else None
        sel = picks[keys.index(key)] if key is not None else None
        item = lines[sel][2] if sel is not None else None

        if (view is None) != (was is None):
            scr.clear()
        else:
            scr.erase()
        body = h - 1
        if view is not None:
            # the details of the row picked, kept up to date while it is there
            view = next((l[2] for l in lines if l[2] and l[2]["key"] == view["key"]), None)
            if view is None:
                message = message or "it has ended"
        if view is not None:
            put(0, label(view), curses.A_BOLD)
            for y, t in enumerate(details(view), 2):
                put(y, t)
        else:
            if sel is not None:
                if sel < top:
                    top = sel
                if sel >= top + body:
                    top = sel - body + 1
            top = max(0, min(top, len(lines) - body))
            for y, (t, attr, it) in enumerate(lines[top:top + body]):
                picked = top + y == sel
                put(y, t, attr | (curses.A_REVERSE if picked else 0), picked)
        if confirm is not None:
            put(h - 1, "End %s? y / n" % label(confirm), curses.A_BOLD | tones["bad"], True)
        else:
            keys_help = ("Enter/Esc back  k end it  q quit" if view is not None else
                         "↑↓ pick  Enter details  k end  h %s  r refresh  q quit"
                         % ("last 6 ended" if state["ended"] is None else "all ended"))
            put(h - 1, (message or state["error"] or keys_help), curses.A_REVERSE, True)
        scr.refresh()
        was = view

        c = scr.getch()
        if c == -1:
            continue
        if c != curses.KEY_RESIZE:
            message = ""
        if confirm is not None:    # anything but y is no
            if c in (ord("y"), ord("Y")):
                message = end(confirm)
                state["wake"].set()
            else:
                message = "nothing ended"
            confirm = None
        elif c in (ord("q"), ord("Q")):
            return
        elif c == 27 or (view is not None and c in (10, 13, curses.KEY_ENTER)):
            view = None    # Esc never quits: a key the terminal sends that curses does not know starts with it
        elif c in (10, 13, curses.KEY_ENTER):
            view = item
        elif c in (ord("k"), curses.KEY_DC):
            it = view or item
            if it is not None and it["what"] != "ended":
                confirm = it
            elif it is not None:
                message = "an ended job cannot be ended"
        elif c in (ord("h"), ord("H")):
            state["ended"] = 6 if state["ended"] is None else None
            state["wake"].set()
        elif c in (ord("r"), ord("R")):
            state["wake"].set()
            message = "refreshing"
        elif keys and view is None:
            n = keys.index(key) if key in keys else 0
            step = {curses.KEY_UP: -1, curses.KEY_DOWN: 1, curses.KEY_PPAGE: -(body // 2),
                    curses.KEY_NPAGE: body // 2, curses.KEY_HOME: -len(keys), curses.KEY_END: len(keys)}.get(c, 0)
            key = keys[max(0, min(len(keys) - 1, n + step))]


def status():
    running, waiting = read_jobs("running"), read_jobs("waiting")
    parts = []
    n = slots(running + waiting)
    for kind, short in (("quartus", "Q"), ("sim", "sim")):
        r = sum(j["kind"] == kind for j in running)
        w = sum(j["kind"] == kind for j in waiting)
        if r or w:
            parts.append("%s %d/%d%s" % (short, r, n[kind], " +%d waiting" % w if w else ""))
    orphans = sum(p["orphan"] for p in strays())
    if orphans:
        parts.append("%d orphan" % orphans)
    print(" · ".join(parts), end="")


def kill(what):
    w = what.lower()

    def matches(it):
        if it["what"] == "job":
            j = it["job"]
            return what == j["id"] or w in (os.path.basename(j["repo"]).lower(), j["name"].lower())
        if it["what"] == "container":
            return len(w) >= 4 and it["container"]["id"].startswith(w)
        if it["what"] == "program":
            return what == str(it["program"]["pid"])
        return False

    found = [r[1] for sec in collect(ended=0)["sections"] for r in sec["rows"] if matches(r[1])]
    if len(found) > 1:
        print("more than one is %s, give its id:" % what)
        for it in found:
            print("  " + label(it))
        return 1
    if not found:
        print("no job, container or program of the framework is %s" % what)
        return 1
    print(end(found[0]))
    return 0


def main():
    args = sys.argv[1:]
    if not args:
        print(show())
    elif args[0] == "-i":
        if not sys.stdout.isatty():
            print("jobs.py -i needs a terminal")
            return 2
        interactive(float(args[1]) if len(args) > 1 else 3)
    elif args[0] == "watch":
        every = float(args[1]) if len(args) > 1 else 5
        try:
            while True:
                text = show()
                sys.stdout.write("\033[H\033[J" + text + "\n")
                sys.stdout.flush()
                time.sleep(every)
        except KeyboardInterrupt:
            pass
    elif args[0] == "history":
        rows = read_history()[-(int(args[1]) if len(args) > 1 else 20):]
        print("\n".join(table([r[0] for r in ended_rows(rows)], ENDED_COLUMNS)) if rows else "no job has ended yet")
    elif args[0] == "kill" and len(args) == 2:
        return kill(args[1])
    elif args[0] == "status":
        status()
    else:
        print(__doc__.strip())
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
