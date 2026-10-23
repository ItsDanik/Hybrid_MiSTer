#!/bin/sh
# Run a long job (a Quartus build, a simulation) in a slot of this PC: the
# agents and builds working in parallel take turns, first come first served,
# instead of all running at once. tools/jobs.py shows them.
# usage: hybrid/tools/job.sh <kind> <name> <command> [arguments]
#   kind  quartus (3 at a time) or sim (4 at a time); DNK_JOB_SLOTS=<n> for
#         another number
#   name  what runs: the Quartus project, the testbench script
# The command runs with DNK_JOB set to the job's id; a job started inside one
# takes no second slot. Its containers get the label dnk.job=$DNK_JOB (the
# scripts' docker run lines), so that ending the job ends them too.
# State in ${XDG_CACHE_HOME:-~/.cache}/dnk-jobs: waiting/ and running/ (a file
# for each job), history.tsv (a line for each job that ended), slots/ (locks).
# A job of a minute or more says on the desktop that it ended (notify-send).
KIND=$1
NAME=$2
shift 2 || { echo "usage: job.sh <quartus|sim> <name> <command> [arguments]" >&2; exit 2; }
[ -n "$DNK_JOB" ] && exec "$@"
case $KIND in
    quartus) SLOTS=3 ;;
    sim) SLOTS=4 ;;
    *) echo "job.sh: kind is quartus or sim, not $KIND" >&2; exit 2 ;;
esac
SLOTS=${DNK_JOB_SLOTS:-$SLOTS}
DIR=${XDG_CACHE_HOME:-$HOME/.cache}/dnk-jobs
mkdir -p "$DIR/slots" "$DIR/waiting" "$DIR/running" || exit 1

# The repository: the core around hybrid/ when the job is the framework's
REPO=$( { [ -f "$1" ] && cd "$(dirname "$1")" ; } 2>/dev/null
    r=$(git rev-parse --show-superproject-working-tree 2>/dev/null)
    [ -n "$r" ] || r=$(git rev-parse --show-toplevel 2>/dev/null)
    echo "${r:-$PWD}")
QUEUED=$(date +%s)
ID=$QUEUED.$$
export DNK_JOB=$ID

info() {
    printf 'kind=%s\nname=%s\nrepo=%s\npid=%s\nsession=%s\nqueued=%s\nstarted=%s\nslot=%s\nslots=%s\ncommand=%s\n' \
        "$KIND" "$NAME" "$REPO" $$ "${CLAUDE_CODE_SESSION_ID:-}" "$QUEUED" "${STARTED:-}" "${SLOT:-}" "$SLOTS" "$*"
}
kill_containers() {
    ids=$(docker ps -q --filter "label=dnk.job=$ID" 2>/dev/null)
    [ -z "$ids" ] || docker kill $ids > /dev/null 2>&1
}
cleanup() {
    rm -f "$DIR/waiting/$ID" "$DIR/running/$ID"
}
# A line of history.tsv: end, start, seconds waited, seconds run, exit code,
# kind, name, repository, session, result (a Quartus build's worst slack)
record() {
    END=$(date +%s)
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$END" "$STARTED" $((STARTED - QUEUED)) $((END - STARTED)) $1 \
        "$KIND" "$NAME" "$REPO" "${CLAUDE_CODE_SESSION_ID:-}" "$2" >> "$DIR/history.tsv"
}
CHILD=
STARTED=
trap '[ -z "$CHILD" ] || { kill $CHILD 2>/dev/null; kill_containers; }; cleanup; [ -z "$STARTED" ] || record 143 ended; exit 143' INT TERM HUP

# Waiting: a slot is taken by the oldest job still waiting for one
info "$@" > "$DIR/waiting/$ID"
SAID=
while :; do
    ahead=0
    for f in "$DIR/waiting/"*; do
        [ -f "$f" ] || continue
        if ! kill -0 "$(sed -n 's/^pid=//p' "$f")" 2>/dev/null; then
            rm -f "$f"    # a job that was killed while it waited
            continue
        fi
        n=${f##*/}
        q=${n%.*}
        p=${n#*.}
        { [ "$q" -lt $QUEUED ] || { [ "$q" = $QUEUED ] && [ "$p" -lt $$ ]; }; } &&
            grep -q "^kind=$KIND\$" "$f" && ahead=$((ahead + 1))
    done
    if [ $ahead = 0 ]; then
        SLOT=1
        while [ $SLOT -le $SLOTS ]; do
            exec 9> "$DIR/slots/$KIND.$SLOT"
            flock -n 9 && break 2
            SLOT=$((SLOT + 1))
        done
        exec 9>&-
    fi
    if [ "$SAID" != $ahead ]; then
        echo "job.sh: waiting for one of the $SLOTS $KIND slots, $ahead job(s) ahead in the queue; hybrid/tools/jobs.py shows them" >&2
        SAID=$ahead
    fi
    sleep 3
done
STARTED=$(date +%s)
rm -f "$DIR/waiting/$ID"
info "$@" > "$DIR/running/$ID"
[ -z "$SAID" ] || echo "job.sh: $KIND slot $SLOT after $((STARTED - QUEUED))s" >&2

# Run in the background so that a signal reaches the trap at once (the shell
# would wait for a command in the foreground first)
"$@" 9>&- &
CHILD=$!
wait $CHILD
RC=$?
CHILD=
cleanup

# A Quartus build's result: the worst slack of the timing summary it just wrote
RESULT=
if [ $KIND = quartus ]; then
    sta=$(find "$REPO" -maxdepth 3 -path "*/output_files/$NAME.sta.summary" -newermt "@$STARTED" 2>/dev/null | head -1)
    [ -z "$sta" ] || RESULT=$(awk '/^Slack/ { if (min == "" || $3 < min) min = $3 } END { if (min != "") print "slack " min }' "$sta")
fi
record $RC "$RESULT"

if [ $((END - STARTED)) -ge 60 ] && command -v notify-send > /dev/null; then
    mins=$(( (END - STARTED + 30) / 60 ))
    if [ $RC = 0 ]; then what="done"; else what="FAILED (exit $RC)"; fi
    notify-send -a "dnk jobs" "$KIND $NAME $what" "${REPO##*/}, ${mins} min${RESULT:+, $RESULT}" > /dev/null 2>&1 &
fi
exit $RC
