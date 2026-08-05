#!/usr/bin/env bash
# UT99 softenh -- native Linux launcher (installed into a run tree by mkrun-ut99.sh).
#
#   ./run.sh                     software renderer, menu
#   ./run.sh gl                  XOpenGLDrv, for side-by-side reference
#   ./run.sh soft DM-Barricade   start straight into a map
#
#   RES=1920x1080   viewport size
#   BILINEAR=0|1    expensive bilinear filtering instead of the cheap knight dither
#                   (both require HighRes/LowResTextureSmooth; smoothing off = point)
#   MESHFOG=0|1     volumetric fog on meshes and actors
#   SURFFOG=0|1     volumetric fog on BSP surfaces
#
# On Wayland, prefix with SDL_VIDEODRIVER=x11 if no window appears.
set -eu
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
INI="$HERE/System/UnrealTournament.ini"

DEV=soft
case "${1:-}" in
    soft) shift ;;
    gl)   DEV=gl; shift ;;
esac

if [ -n "${RES:-}" ]; then
    sed -i -E "s|^WindowedViewportX=.*|WindowedViewportX=${RES%x*}|;
               s|^WindowedViewportY=.*|WindowedViewportY=${RES#*x}|" "$INI"
fi
[ -n "${BILINEAR:-}" ] && sed -i -E "s|^BilinearFiltering=.*|BilinearFiltering=$([ "$BILINEAR" = 1 ] && echo True || echo False)|" "$INI"
[ -n "${MESHFOG:-}" ] && sed -i -E "/^\[SoftDrv.SoftwareRenderDevice\]/,/^\[/ \
    s|^VolumetricLightingMeshes=.*|VolumetricLightingMeshes=$([ "$MESHFOG" = 1 ] && echo True || echo False)|" "$INI"
[ -n "${SURFFOG:-}" ] && sed -i -E "/^\[SoftDrv.SoftwareRenderDevice\]/,/^\[/ \
    s|^VolumetricLighting=.*|VolumetricLighting=$([ "$SURFFOG" = 1 ] && echo True || echo False)|" "$INI"

# SoftDrv is never auto-adopted (ufront 2.9 will not promote a software device to the hardware
# default), so it has to be selected on the command line, not through the ini.
ARGS=""
[ "$DEV" = soft ] && ARGS="-SOFTWARERENDERER"

cd "$HERE/System"
exec env LD_LIBRARY_PATH="$HERE/System:${LD_LIBRARY_PATH:-}" \
    ./UnrealTournament "$@" $ARGS -log
