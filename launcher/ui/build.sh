#!/bin/sh
# Build danik_hybrid_ports_ui for the MiSTer (static, ARM) in the toolchain
# container, from the framework's directory. The font table first, from
# assets/cozette.bdf. The version the screen shows is the launcher's VERSION.
# usage: launcher/ui/build.sh [out]   (default launcher/ui/danik_hybrid_ports_ui)
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-launcher/ui/danik_hybrid_ports_ui}
VERSION=$(sed -n 's/^VERSION=\([0-9][0-9]*\)$/\1/p' launcher/danik_hybrid_ports.sh)
[ -n "$VERSION" ] || { echo "no VERSION in launcher/danik_hybrid_ports.sh" >&2; exit 1; }
python3 launcher/ui/genfont.py > launcher/ui/cozette.h
sh docker.sh arm-linux-gnueabihf-gcc -O2 -static -Wall -Wextra -Wno-stringop-overflow -DLAUNCHER_VERSION="$VERSION" -o "$OUT" launcher/ui/danik_hybrid_ports_ui.c
sh docker.sh arm-linux-gnueabihf-strip "$OUT"
ls -la "$OUT"
