#!/usr/bin/env bash
# shot.sh -- run the native UT99 build headlessly under Xvfb and grab a frame.
#
#   UT99_RUN=<run-tree> tools/dev/shot.sh <Map.unr> <out.png> [seconds] [extra engine args...]
#
#   UT99_RUN   the run tree (mkrun-ut99.sh makes one); required: the script deletes its log and, with a
#              device name in UT99_DEV, rewrites its render device
#   UT99_DEV   soft (default), gl (the ini's device), or a render device class name
#   UT99_RES   the Xvfb screen as WxH (default: the ini's windowed viewport size)
#   UT99_DISP  the Xvfb display (default :99)
#
# Xvfb's -fbdir dump is used instead of the engine's SHOT command, which needs a keypress.
#
# Writes <out.png> and <out.png>.log (the engine log for exactly that run), and prints the render
# device the engine actually selected (a software device in the ini is not adopted as a default).
set -u

MAP="${1:?usage: shot.sh <Map.unr> <out.png> [seconds] [args...]}"
OUT="${2:?}"
SECS="${3:-20}"
shift 3 || true

RUN="${UT99_RUN:?set UT99_RUN to the run tree (tools/dev/mkrun-ut99.sh makes one)}"
DISP=${UT99_DISP:-:99}
# Match Xvfb to the engine's own viewport size: otherwise the engine ticks two viewports and the
# captured frame is whichever was presented last.
_VPX=$(sed -nE 's/^WindowedViewportX=([0-9]+).*/\1/p' "$RUN/System/UnrealTournament.ini" 2>/dev/null | head -1)
_VPY=$(sed -nE 's/^WindowedViewportY=([0-9]+).*/\1/p' "$RUN/System/UnrealTournament.ini" 2>/dev/null | head -1)
RES=${UT99_RES:-${_VPX:-800}x${_VPY:-600}}
DEV=${UT99_DEV:-soft}
FBDIR=$(mktemp -d)
XWD=$(mktemp -u)/fb.xwd
mkdir -p "$(dirname "$XWD")"

case "$DEV" in
    soft) DEVARG="-SOFTWARERENDERER" ;;
    gl)   DEVARG="" ;;
    *)    DEVARG=""
          sed -i -E "s#^(Game|Windowed)?RenderDevice=.*#\1RenderDevice=${DEV}#" \
              "$RUN/System/UnrealTournament.ini" 2>/dev/null || true ;;
esac

LOG="$RUN/System/UnrealTournament.log"
rm -f "$LOG"

Xvfb "$DISP" -screen 0 "${RES}x24" -fbdir "$FBDIR" >/dev/null 2>&1 &
XPID=$!
sleep 1

# SDL_RENDER_DRIVER=software is required headlessly: SDL_CreateRenderer otherwise picks a
# GL-backed renderer that composites nothing under Xvfb, and SoftDrv's frames never reach the
# window (the capture comes out white).
( cd "$RUN/System" && DISPLAY="$DISP" SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software \
    LD_LIBRARY_PATH="$RUN/System:${LD_LIBRARY_PATH:-}" \
    exec ./UnrealTournament "$MAP" -log $DEVARG "$@" >/tmp/ut99run.log 2>&1 ) &
GPID=$!	# exec: the subshell becomes the game, so the kill below reaches it

sleep "$SECS"

# Fail rather than convert a stale dump from a previous run.
cp "$FBDIR/Xvfb_screen0" "$XWD" 2>/dev/null || true
kill "$GPID" 2>/dev/null; wait "$GPID" 2>/dev/null
kill "$XPID" 2>/dev/null; wait "$XPID" 2>/dev/null

[ -f "$LOG" ] && cp -f "$LOG" "$OUT.log"

SEL=$(grep -oiE '(selects|persisted user-selected|default) render device [A-Za-z.]+' "$OUT.log" 2>/dev/null | tail -1)
[ -z "$SEL" ] && SEL=$(grep -oiE 'RenderDevice: [A-Za-z.]+' "$OUT.log" 2>/dev/null | tail -1)

if [ -s "$XWD" ]; then
    ffmpeg -y -loglevel error -i "$XWD" -update 1 -frames:v 1 "$OUT" && echo "wrote $OUT   [${SEL:-renderer?}]"
else
    echo "FAILED: no framebuffer dump for $OUT (engine died?)" >&2
    grep -iE 'critical|appError|Failed to' "$OUT.log" 2>/dev/null | head -3 >&2
    rm -rf "$FBDIR" "$(dirname "$XWD")"
    exit 1
fi
rm -rf "$FBDIR" "$(dirname "$XWD")"
