#!/usr/bin/env bash
# shot.sh -- run the native UT99 build headlessly under Xvfb and grab a frame.
#
#   tools/dev/shot.sh <Map.unr> <out.png> [seconds] [extra engine args...]
#   U1RUN=<run-tree> U1DEV=soft|gl U1RES=WxH U1DISP=:N tools/dev/shot.sh ...
#
# Xvfb's -fbdir dump is used instead of the engine's SHOT command: SHOT needs a keypress and there
# is no key-injection tool on this box.
#
# Writes <out.png> and <out.png>.log (the engine log for exactly that run), and prints the render
# device the engine actually selected -- do not trust the ini, ufront 2.9 refuses to promote a
# software device to the hardware default and silently falls back to GL.
set -u

MAP="${1:?usage: shot.sh <Map.unr> <out.png> [seconds] [args...]}"
OUT="${2:?}"
SECS="${3:-20}"
shift 3 || true

RUN=${U1RUN:-/home/ion/native-runtrees/ut99-softenh}
DISP=${U1DISP:-:99}
# Match Xvfb to the engine's own viewport size. If they differ the engine ends up ticking two
# viewports (800x600 and 640x480 both show up in the AutoFOV log) and the captured frame is
# whichever was presented last -- which varies between builds and silently swamps any real
# comparison. A colour-only change measured 17 dB apart on a mismatched screen and 43 dB apart on a
# matched one; the 17 dB was entirely HUD/geometry displacement from the wrong viewport.
_VPX=$(sed -nE 's/^WindowedViewportX=([0-9]+).*/\1/p' "$RUN/System/UnrealTournament.ini" 2>/dev/null | head -1)
_VPY=$(sed -nE 's/^WindowedViewportY=([0-9]+).*/\1/p' "$RUN/System/UnrealTournament.ini" 2>/dev/null | head -1)
RES=${U1RES:-${_VPX:-800}x${_VPY:-600}}
DEV=${U1DEV:-soft}
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
# window -- the capture comes out pure white with the engine happily rendering behind it.
( cd "$RUN/System" && DISPLAY="$DISP" SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software \
    LD_LIBRARY_PATH="$RUN/System:${LD_LIBRARY_PATH:-}" \
    ./UnrealTournament "$MAP" -log $DEVARG "$@" >/tmp/ut99run.log 2>&1 ) &
GPID=$!

sleep "$SECS"

# Fail loudly rather than silently converting a stale dump from a previous run.
cp "$FBDIR/Xvfb_screen0" "$XWD" 2>/dev/null || true
kill "$GPID" 2>/dev/null; wait "$GPID" 2>/dev/null
kill "$XPID" 2>/dev/null; wait "$XPID" 2>/dev/null

[ -f "$LOG" ] && cp -f "$LOG" "$OUT.log"

SEL=$(grep -oiE 'default render device [A-Za-z.]+' "$OUT.log" 2>/dev/null | tail -1)
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
