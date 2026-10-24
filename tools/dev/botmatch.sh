#!/usr/bin/env bash
# botmatch.sh -- a headless native bot match: a dedicated server, one client that joins it, and a poll of
# the server's query port. Prints one summary line per run tree.
#
#   tools/dev/botmatch.sh <server-run-tree> <seconds> [<client-run-tree>] [<map>]
#
# Bots come from [Botpack.DeathMatchPlus] MinPlayers in the server tree's UnrealTournament.ini (it is
# globalconfig, not a URL option); a dedicated server only adds them once a human has joined. The server
# port is that ini's [URL] Port; the query port is Port+1. The client may be another run tree, e.g. a
# 32-bit client joining a 64-bit server. Output lands in <server-run-tree>/botmatch/.
#
# `timeout xvfb-run ...` would kill the wrapper and leave the game running, so the timeout is inside;
# stdout and the engine log are block-buffered, so every process runs under `stdbuf -oL`.
set -eu
SRV=$(readlink -f "${1:?usage: botmatch.sh <server-run-tree> <seconds> [<client-run-tree>] [<map>]}")
SECS=${2:?}
CLI=$(readlink -f "${3:-$SRV}")
MAP=${4:-DM-Deck16][}
OUT=$SRV/botmatch; mkdir -p "$OUT"
PORT=$(sed -n '/^\[URL\]/,/^\[/{s/^Port=\([0-9]*\).*/\1/p}' "$SRV/System/UnrealTournament.ini" | head -1)
PORT=${PORT:-7777}
QPORT=$((PORT+1))

cd "$SRV/System"
timeout $((SECS+60)) stdbuf -oL -eL ./ucc-bin server "$MAP?game=Botpack.DeathMatchPlus" > "$OUT/server.log" 2>&1 &
SPID=$!
for _ in $(seq 1 60); do grep -q "Game engine initialized" "$OUT/server.log" 2>/dev/null && break; sleep 1; done

cd "$CLI/System"
xvfb-run -a -s "-screen 0 1024x768x24" timeout $((SECS+30)) stdbuf -oL -eL \
    ./UnrealTournament "127.0.0.1:$PORT" -NULLRENDERER > "$OUT/client.log" 2>&1 &
CPID=$!

q() { python3 - "$QPORT" "$1" <<'PY'
import socket, sys
k = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); k.settimeout(1.5)
k.sendto(sys.argv[2].encode(), ('127.0.0.1', int(sys.argv[1])))
try:
    d = k.recvfrom(65535)[0].decode('latin1').split('\\'); print(d[2] if len(d) > 2 else '?')
except Exception:
    print('DOWN')
PY
}
: > "$OUT/poll.txt"
T0=$SECONDS
while [ $((SECONDS-T0)) -lt "$SECS" ]; do
    echo "$((SECONDS-T0))s time=$(q '\level_property\TimeSeconds\') bots=$(q '\game_property\NumBots\') humans=$(q '\game_property\NumPlayers\') ended=$(q '\game_property\bGameEnded\')" >> "$OUT/poll.txt"
    sleep 15
done
# Stop the game, not the xvfb-run wrapper: xvfb-run then shuts its own X server down (killing the wrapper
# orphans Xvfb). The server's `timeout` forwards SIGTERM to ucc-bin. (No `pkill -f` on the map: it is a
# regex and a map like DM-Deck16][ is not a valid one.)
pkill -f "^./UnrealTournament 127.0.0.1:$PORT " 2>/dev/null || true
kill "$SPID" 2>/dev/null || true
wait 2>/dev/null || true

NG=$(ls -t "$SRV"/Logs/Unreal.ngLog.* 2>/dev/null | head -1 || true)
KILLS=0
[ -n "$NG" ] && KILLS=$(tr -d '\000' < "$NG" | grep -ac "	kill	" || true)
BAD='SIGSEGV|Signal|Assertion failed|appError|Critical|General protection|Segmentation fault|\[layout\]'
echo "server=$(file -b "$SRV/System/ucc-bin" | cut -d, -f1-2) client=$(file -b "$CLI/System/UnrealTournament" | cut -d, -f1-2)"
echo "last poll: $(tail -1 "$OUT/poll.txt")"
echo "joined=$(grep -c 'Join succeeded' "$OUT/server.log") kills>=$KILLS accessed_none=$(grep -c 'Accessed None' "$OUT/server.log") warnings=$(grep -ci 'warning' "$OUT/server.log") crash_markers=$(grep -cE "$BAD" "$OUT/server.log" "$OUT/client.log" | awk -F: '{s+=$2} END{print s}')"
