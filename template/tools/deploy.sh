#!/bin/sh
# Package the build and copy it to a MiSTer over ssh.
# usage: MISTER=root@<address> tools/deploy.sh
# The core is not (re)loaded: load @NAME@ from the menu afterwards. Run
# /media/fat/Scripts/danik_hybrid_cores.sh on the MiSTer once to set up the launcher.
set -e
cd "$(dirname "$0")/.."
HOST=${MISTER:?set MISTER=root@<address of the MiSTer>}
./package.sh > /dev/null
OUT=dist/@NAME@_$(date +%Y%m%d)
G=/media/fat/games/@NAME@
ssh "$HOST" "mkdir -p $G && rm -f /media/fat/_Other/@NAME@_*.rbf"
# a running game keeps its binary busy: copy next to it, then swap
scp -q "$OUT/games/@NAME@/@NAME@" "$HOST:$G/@NAME@.new"
ssh "$HOST" "mv -f $G/@NAME@.new $G/@NAME@"
# everything else of the package (user files on the MiSTer are left alone)
(cd "$OUT/games/@NAME@" && tar cf - --exclude=./@NAME@ .) | ssh "$HOST" "tar xf - --no-same-owner -C $G"
scp -q "$OUT/Scripts/danik_hybrid_cores.sh" "$HOST:/media/fat/Scripts/"
scp -q "$OUT"/_Other/*.rbf "$HOST:/media/fat/_Other/"
ssh "$HOST" "chmod +x $G/@NAME@ $G/*.sh /media/fat/Scripts/danik_hybrid_cores.sh; ls -la $G /media/fat/_Other/@NAME@_*.rbf"
