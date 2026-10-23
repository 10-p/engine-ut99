#!/usr/bin/env bash
# run.sh -- run the native build under Xvfb with the input test channel and feed it a scenario.
#
#   tools/dev/inputtest/run.sh <run-tree> <scenario> <out-dir> [engine args...]
#   tools/dev/inputtest/run.sh run turn.scen out/turn 'DM-Deck16][?Game=Botpack.DeathMatchPlus' -SOFTWARERENDERER
#
# A scenario is a list of the channel's commands (Source/NSDLDrv/Src/NSDLInputTest.h), one per line, with two
# more that run here: "sleep <seconds>" and "waitlog <extended regex>" (wait until the log matches it, 120 s at
# most). Lines starting with # are comments. The game's log goes to <out-dir>/game.log, the commands it was
# given to <out-dir>/cmds.txt; rate.py beside this script reads the watch lines of the log.
#
# Xvfb takes the first free display (-displayfd); the game and Xvfb are stopped when the scenario ends.
set -u
RUN="$(cd "${1:?usage: run.sh <run-tree> <scenario> <out-dir> [engine args...]}" && pwd)"
SCEN="$(cd "$(dirname "${2:?}")" && pwd)/$(basename "$2")"
OUT="${3:?}"
shift 3
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
CMD="$OUT/cmds.txt"
LOG="$OUT/game.log"
: > "$CMD"
: > "$LOG"

Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$OUT/display" 2>"$OUT/xvfb.log" &
XPID=$!
for _ in $(seq 50); do [ -s "$OUT/display" ] && break; sleep 0.1; done
DISP=":$(cat "$OUT/display")"

cd "$RUN/System"
DISPLAY="$DISP" LD_LIBRARY_PATH="$RUN/System:${LD_LIBRARY_PATH:-}" stdbuf -oL \
    ./UnrealTournament "$@" -log -INPUTTEST="$CMD" > "$LOG" 2>&1 &
GPID=$!

while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
        "#"*|"") ;;
        sleep\ *) sleep "${line#sleep }" ;;
        waitlog\ *)
            re="${line#waitlog }"
            for _ in $(seq 600); do
                grep -qE "$re" "$LOG" && break
                kill -0 $GPID 2>/dev/null || { echo "run.sh: the game ended before: $re" >&2; break; }
                sleep 0.2
            done ;;
        *) printf '%s\n' "$line" >> "$CMD" ;;
    esac
done < "$SCEN"

sleep 1
kill -TERM $GPID 2>/dev/null
for _ in $(seq 50); do kill -0 $GPID 2>/dev/null || break; sleep 0.1; done
kill -KILL $GPID 2>/dev/null
kill -TERM $XPID 2>/dev/null
wait 2>/dev/null
echo "$LOG"
