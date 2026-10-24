#!/usr/bin/env bash
# play.sh <run-tree> <device-switch> <url> <out-dir> [<console command>...] -- a scripted player session,
# for the paths bots and -NULLRENDERER do not reach (alias keys, the software renderer's palettes, `shot`).
# It boots <url> on an X display, presses keys bound to aliases (Fire, AltFire, movement, Jump),
# turns two full circles (procedural and scripted textures only tick when seen), types each command into the
# console with a screenshot after it, and ends with `exit`.
#
#   Xvfb :98 -screen 0 1920x1080x24 -nolisten tcp &          # no WM: xdotool drives XTEST input directly
#                                                            # (another Xvfb: PLAY_DISPLAY=:<n>)
#   unshare -rn tools/dev/census/play.sh <tree> -SOFTWARERENDERER "DM-Morpheus?game=Botpack.DeathMatchPlus" \
#       /tmp/out "stat global" "rmode 1" "rmode 5" shot "savegame 0" "setres 400x300"
#
# Run it under `unshare -rn`: loopback only, nothing can reach a real server. The game runs on a pty
# (`script -f`), so its log is line-buffered at either width (stdbuf preloads a 64-bit library, which a
# 32-bit game cannot load). Screenshots come from the X root (xwd), not the game.
# Known quirk: after some commands the console stays open, the final `exit` toggles it shut instead and the
# game is stopped with SIGTERM ("did not exit") -- harmless.
set -u
TREE=$1; SW=$2; URL=$3; OUT=$4; shift 4
mkdir -p "$OUT"
ip link set lo up 2>/dev/null || true
# Never the desktop's DISPLAY: the session types and clicks into whatever display it runs on.
export DISPLAY=${PLAY_DISPLAY:-:98} XAUTHORITY=/dev/null SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy ALSOFT_DRIVERS=null
LOG="$OUT/client.log"
MAP=${URL%%\?*}
cd "$TREE/System"
script -qfc "timeout 900 ./UnrealTournament '$URL' $SW" "$LOG" > /dev/null 2>&1 &

game()     { pgrep -f "^./UnrealTournament .*$SW"; }
stopgame() { pkill -TERM -f "^./UnrealTournament .*$SW" 2>/dev/null; sleep 3; }
wait_log() { for _ in $(seq 1 "${2:-60}"); do grep -qF -- "$1" "$LOG" 2>/dev/null && return 0; sleep 1; done
             echo "TIMEOUT waiting for: $1"; return 1; }
shot()  { xwd -root -silent | ffmpeg -loglevel error -y -f xwd_pipe -i - "$OUT/$1.png"; }
k()     { xdotool key "$@"; sleep 0.5; }
hold()  { xdotool keydown "$1"; sleep "$2"; xdotool keyup "$1"; sleep 0.3; }
click() { xdotool mousemove 600 330; sleep 0.1; xdotool mousemove 640 360; sleep 0.1   # UWindow ignores a bare click
          xdotool mousedown "$1"; sleep 0.15; xdotool mouseup "$1"; sleep 0.4; }
con()   { k grave; xdotool type --delay 40 "$1"; sleep 0.2; k Return; sleep "${2:-2}"; }

wait_log "Bringing Level $MAP" 120 || { stopgame; exit 1; }
sleep 8
WID=$(xdotool search --onlyvisible --name '.*' | tail -1)
xdotool windowmove "$WID" 0 0; xdotool windowfocus --sync "$WID"; sleep 0.5
shot 00-arrive
click 1; click 3; hold w 1; hold a 0.5; hold s 0.5; hold d 0.5; k space     # keys bound to aliases
for lap in 1 2; do
    for _ in $(seq 1 12); do xdotool mousemove_relative -- 60 0; sleep 0.25; done
    hold w 1.5
    shot "01-lap$lap"
done
n=2
for c in "$@"; do
    con "$c" 3
    shot "$(printf '%02d' $n)-$(echo "$c" | tr -c 'A-Za-z0-9' '_' | cut -c1-24)"
    n=$((n+1))
    game > /dev/null || { echo "GAME GONE after: $c"; break; }
done
if game > /dev/null; then
    con "exit" 1
    for _ in $(seq 1 45); do game > /dev/null || break; sleep 1; done
    game > /dev/null && { echo "did not exit: SIGTERM"; stopgame; }
fi
wait 2>/dev/null
BAD='SIGSEGV|Signal|Assertion failed|appError|Critical|General protection|Segmentation fault|\[layout\]'
echo "exit: $(grep -aoE 'COMMAND_EXIT_CODE="[0-9]+"' "$LOG")  crash markers: $(grep -caE "$BAD" "$LOG")"
