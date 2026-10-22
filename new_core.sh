#!/bin/sh
# Start a new hybrid core repository from template/.
#
# In the new, empty directory:
#   git init
#   git submodule add -b main https://github.com/ItsDanik/Hybrid_MiSTer.git hybrid
#   hybrid/new_core.sh <Name> "<Game title>" [<url of the game's git repository>]
#
#   <Name>   the engine's name, one word, the source port's (never a game's
#            alone), as it is to appear everywhere: OSD, _Other/dnk/
#            dnk-<Name>_YYYYMMDD.rbf, games/dnk/<Name>/ (ECWolf, Dethrace)
#   <Game title>  its game, the menu entry's name (_Other/dnk-<title>.mgl);
#            more games: package/games.txt
#   <url>    our fork of the game; added as the submodule <name>/ (the name in
#            lower case) on its branch "mister"
#
# Writes into the repository this hybrid/ is a submodule of, and refuses if
# that already has a core. Nothing is committed.
set -e
HYBRID=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HYBRID")
NAME=$1
TITLE=$2
GAME_URL=$3

case "$NAME" in
    "" | *[!A-Za-z0-9_]*) echo "usage: hybrid/new_core.sh <Name> \"<Game title>\" [<game git url>]" >&2; exit 1 ;;
esac
[ -n "$TITLE" ] || { echo "the game's title is missing" >&2; exit 1; }
[ "$(basename "$HYBRID")" = hybrid ] && [ -e "$ROOT/.git" ] ||
    { echo "$HYBRID is not the hybrid/ submodule of a repository" >&2; exit 1; }
for f in core build.sh package.sh package CLAUDE.md README.md; do
    [ ! -e "$ROOT/$f" ] || { echo "$ROOT/$f exists: this repository already has a core" >&2; exit 1; }
done

LOWER=$(printf %s "$NAME" | tr '[:upper:]' '[:lower:]')
REPO=${NAME}_MiSTer
# for sed: the title may have & or | in it
TITLE_RE=$(printf %s "$TITLE" | sed 's/[&|\\]/\\&/g')
# the game's id in package/games.txt: its title's letters and digits
ID=$(printf %s "$TITLE" | tr -cd 'A-Za-z0-9')

cd "$ROOT"
cp -r "$HYBRID/template/." .
mv gitignore .gitignore
rm -r game
cp "$HYBRID/LICENSE" LICENSE
mkdir -p releases package/logos

# @NAME@ in file and directory names, deepest first
find . -depth -name '*@NAME@*' -not -path './hybrid/*' -not -path './.git/*' | while read -r f; do
    mv "$f" "$(dirname "$f")/$(basename "$f" | sed "s/@NAME@/$NAME/g")"
done
grep -rlE '@(NAME|name|TITLE|REPO|ID)@' . --exclude-dir=hybrid --exclude-dir=.git | while read -r f; do
    sed -i "s|@NAME@|$NAME|g; s|@name@|$LOWER|g; s|@TITLE@|$TITLE_RE|g; s|@REPO@|$REPO|g; s|@ID@|$ID|g" "$f"
done

if [ -n "$GAME_URL" ]; then
    git submodule add -b mister "$GAME_URL" "$LOWER" || {
        echo "Could not add $GAME_URL on its branch \"mister\"."
        echo "Create the branch in the fork, then: git submodule add -b mister $GAME_URL $LOWER"
    }
fi

cat <<END

$NAME is set up in $ROOT. Next:
  - the game: submodule $LOWER/ (branch mister). Its MiSTer code goes behind a
    MISTER_HYBRID build option; hybrid/template/game/ has a starting point.
  - build.sh, package.sh, package/games/$NAME/danik_hybrid_launch.sh: the lines
    marked GAME:
  - package/games.txt: the games (one line each); the launcher's screen
    offers them once the port is in the database
  - package/logos/<id>.png for every game of games.txt ($ID.png to begin
    with): its logo, 440x200, shown at its start (hybrid/docs/game_logo.md;
    package.sh refuses without). See one as the core will show it:
    hybrid/tools/make_logo.py <png> /tmp/logo.mhl --preview out.png --title "$TITLE"
  - core/$NAME.sv: the game's options, "J1" and "jn" in CONF_STR. Repeat "jn"
    in danik_hybrid_launch.sh (MISTER_HYBRID_JN).
  - README.md and package/games/$NAME/README.txt: every TODO
  - hybrid/README.md has the conventions; ./core/build_core.sh builds the core
    as it is (the MiSTer logo until a game shows a frame).
END
