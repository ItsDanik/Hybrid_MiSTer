#!/usr/bin/env python3
"""Read a player's diagnostic report (docs/diagnostics.md).

usage: diag_read.py <report.zip>             its summary and a graph of the 60 seconds
       diag_read.py <report.zip> <other.zip>  the two reports' numbers side by side

The graph has a line a second: how late the latest frame came in its field
(a bar to 16.7ms, the length of a field), the frames never shown (n) and
shown twice (t), the game thread's CPU and how busy CPU0 was."""
import csv
import io
import sys
import zipfile


def load(path):
    z = zipfile.ZipFile(path)
    files = {n: z.read(n).decode("utf-8", "replace") for n in z.namelist() if not n.endswith(".png")}
    rows = list(csv.DictReader(io.StringIO(files.get("pacing.csv", ""))))
    return z, files, rows


def graph(rows):
    print("second  late (ms)                     never/twice  game cpu  cpu0  cpu1  music")
    for r in rows:
        late = float(r["latest_ms"])
        bar = "#" * min(25, int(late / 16.7 * 25 + 0.5))
        marks = "n" * int(r["never_shown"]) + "t" * int(r["shown_twice"])
        print("%6s  %5.1f %-25s %-12s %7s%% %4s%% %4s%%  %s%s" % (
            r["second"], late, bar, marks[:12], r["game_cpu"], r["cpu0"], r["cpu1"], r["music"],
            "  OSD open" if r["osd_open"] == "1" else ""))


def numbers(rows):
    n = len(rows) or 1
    total = lambda k: sum(int(r[k]) for r in rows)
    return [
        ("seconds", len(rows)),
        ("fields", total("fields")),
        ("frames", total("frames")),
        ("never shown", total("never_shown")),
        ("shown twice", total("shown_twice")),
        ("latest in a field (ms)", max((float(r["latest_ms"]) for r in rows), default=0)),
        ("game thread CPU, average %", total("game_cpu") // n),
        ("helper threads CPU, average %", total("helper_cpu") // n),
        ("CPU0 busy, average %", total("cpu0") // n),
        ("CPU1 busy, average %", total("cpu1") // n),
        ("audio ran dry", total("audio_underruns")),
    ]


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    if len(sys.argv) == 2:
        z, files, rows = load(sys.argv[1])
        print(files.get("summary.txt", "(no summary.txt)"))
        graph(rows)
        print("\nFiles: " + ", ".join(z.namelist()))
        return
    a, b = load(sys.argv[1])[2], load(sys.argv[2])[2]
    print("%-32s %14s %14s" % ("", "first", "second"))
    for (name, x), (_, y) in zip(numbers(a), numbers(b)):
        print("%-32s %14s %14s" % (name, x, y))


main()
