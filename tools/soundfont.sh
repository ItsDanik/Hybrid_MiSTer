#!/bin/sh
# The SoundFont every port whose games play MIDI brings (MIDI_SYNTH 1 in its
# core/<Name>.sv; hybrid/README.md, "MIDI and the MT32-pi"): GeneralUser GS by
# S. Christian Collins, its version pinned here, into <folder> (the zip's
# games/dnk/SoundFonts/) with its licence next to it.
#
# It is never committed, in no repository: package.sh fetches it from the
# author's GitHub at the commit below, checks it against the SHA-256 sums,
# and keeps it in the core's build/soundfont/ for the next package. The
# licence lets it be shared and asks that it be shared from our own copy, not
# by a link to the author's files: players get the database's copy.
#
# A new version of GeneralUser GS: COMMIT, both sums and the sizes below, its
# licence read again, and every port with MIDI packaged again (the database
# takes the font only when every port that brings it brings the same bytes).
#
# usage: hybrid/tools/soundfont.sh <folder>   from the core's repository
set -e
OUT=$1
[ -n "$OUT" ] || { echo "usage: hybrid/tools/soundfont.sh <folder>" >&2; exit 1; }
# GeneralUser GS v2.0.3 (2026-02-23)
COMMIT=684543d5e5efaef08d02be50dcda8d552478fa60
URL=https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/$COMMIT
SF2_SUM=9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe
LICENSE_SUM=7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be
CACHE=build/soundfont
mkdir -p "$CACHE" "$OUT"

# <path upstream> <file in the cache> <sum>
fetch() {
    if ! echo "$3  $CACHE/$2" | sha256sum -c --status 2> /dev/null; then
        rm -f "$CACHE/$2.part"
        curl -fsSL --retry 3 -o "$CACHE/$2.part" "$URL/$1"
        echo "$3  $CACHE/$2.part" | sha256sum -c --status ||
            { echo "$URL/$1: not the file of commit $COMMIT (its SHA-256 differs)" >&2; rm -f "$CACHE/$2.part"; exit 1; }
        mv "$CACHE/$2.part" "$CACHE/$2"
    fi
}
fetch GeneralUser-GS.sf2 GeneralUser-GS.sf2 "$SF2_SUM"
fetch documentation/LICENSE.txt GeneralUser-GS-LICENSE.txt "$LICENSE_SUM"
cp "$CACHE/GeneralUser-GS.sf2" "$CACHE/GeneralUser-GS-LICENSE.txt" "$OUT/"
