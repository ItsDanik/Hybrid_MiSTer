#!/usr/bin/env python3
# Builds the database that lets MiSTer's Downloader (update_all) keep the
# launcher up to date: db.json.zip, published on the "db" branch by
# .github/workflows/db.yml whenever the launcher changes.
#
# usage: make_db.py <commit> <output directory>
#
# The file is served from the given commit, so the hash in the database always
# belongs to the file behind the URL.
import hashlib
import json
import os
import sys
import time
import zipfile

REPO = "ItsDanik/Hybrid_MiSTer"
# path on the SD card: path in this repository
FILES = {"Scripts/danik_hybrid_cores.sh": "launcher/danik_hybrid_cores.sh"}


def main():
    commit, out = sys.argv[1], sys.argv[2]
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    raw = "https://raw.githubusercontent.com/%s/" % REPO
    files = {}
    folders = {}
    for dest, src in FILES.items():
        with open(os.path.join(root, src), "rb") as f:
            data = f.read()
        files[dest] = {
            "hash": hashlib.md5(data).hexdigest(),
            "size": len(data),
            "url": raw + commit + "/" + src,
        }
        folders[os.path.dirname(dest)] = {}
    db = {
        "db_id": REPO,
        "db_url": raw + "db/db.json.zip",
        "base_files_url": raw + commit + "/",
        "timestamp": int(time.time()),
        "files": files,
        "folders": folders,
        "v": 1,
    }
    os.makedirs(out, exist_ok=True)
    with zipfile.ZipFile(os.path.join(out, "db.json.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("db.json", json.dumps(db, indent=1, sort_keys=True))
    print(json.dumps(db, indent=1, sort_keys=True))


main()
