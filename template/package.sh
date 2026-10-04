#!/bin/sh
# Assemble the MiSTer release in dist/@NAME@_<date>/ from the build outputs
set -e
cd "$(dirname "$0")"
DATE=$(date +%Y%m%d)
OUT=dist/@NAME@_$DATE
rm -rf "$OUT"
mkdir -p "$OUT/_Other" "$OUT/games/@NAME@" "$OUT/Scripts"
cp hybrid/launcher/danik_hybrid_cores.sh "$OUT/Scripts/danik_hybrid_cores.sh"
# a copy with the game too: the daemon installs the newest one it finds
cp hybrid/launcher/danik_hybrid_cores.sh "$OUT/games/@NAME@/danik_hybrid_cores.sh"
cp core/output_files/@NAME@.rbf "$OUT/_Other/@NAME@_$DATE.rbf"
cp -r package/games/@NAME@/. "$OUT/games/@NAME@/"
cp build/mister/@name@ "$OUT/games/@NAME@/@NAME@"
# GAME: files the binary needs next to it (never game data)
hybrid/docker.sh arm-linux-gnueabihf-strip "$OUT/games/@NAME@/@NAME@"
chmod +x "$OUT/games/@NAME@/@NAME@" "$OUT"/games/@NAME@/*.sh "$OUT/Scripts/danik_hybrid_cores.sh"
# GAME: the game's license as LICENSE-@name@.txt
cp build/sdl/src/SDL2-*/LICENSE.txt "$OUT/games/@NAME@/LICENSE-sdl.txt"
cp LICENSE "$OUT/games/@NAME@/LICENSE-gpl3.txt"
rm -f "dist/@NAME@_$DATE.zip"
(cd "$OUT" && python3 -m zipfile -c "../@NAME@_$DATE.zip" _Other games Scripts)
find "$OUT" -type f | sort
ls -la "dist/@NAME@_$DATE.zip"
