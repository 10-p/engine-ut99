#!/usr/bin/env bash
# mkrun-ut99.sh -- assemble a private UT99 run tree for the native build.
#
#   tools/dev/mkrun-ut99.sh <build-dir> <run-dir> [<extra-maps-dir>]
#
# Paths default to the work folder's layout, the folder three levels above this script ($WORKUNREAL
# overrides it): assets from $W/assets/ut99-assets ($UT99_ASSETS_DIR), the reference run tree
# $W/native-runtrees/ut99 ($UT99_REF_RUNTREE). The *.unr files of <extra-maps-dir> (or $UT99_EXTRA_MAPS;
# no default) are copied into Maps/.
#
# The asset tree may be shared and read-only, so nothing here writes into it: big read-only trees are
# symlinked, and System/ is a real copy because the engine rewrites inis and logs into it.
#
# --remove-destination on every copy is required: a plain `cp` onto a symlink follows the link and writes
# into the shared asset tree.
set -eu

BUILD="${1:?usage: mkrun-ut99.sh <build-dir> <run-dir> [<extra-maps-dir>]}"
RUN="${2:?}"
EXTRA_MAPS="${3:-${UT99_EXTRA_MAPS:-}}"
W="${WORKUNREAL:-$(cd "$(dirname "$0")/../../.." && pwd)}"
# Prefer the reference run tree's real asset directories over the asset tree, which may be unmounted
# between sessions (a run tree symlinked into it would dangle).
A="${UT99_ASSETS_DIR:-$W/assets/ut99-assets}"
REF="${UT99_REF_RUNTREE:-$W/native-runtrees/ut99}"
[ -d "$REF/Maps" ] && [ ! -d "$A/Maps" ] && A="$REF"

rm -rf "$RUN"
mkdir -p "$RUN"

# Read-only bulk: symlinked, never written.
for d in Textures Sounds Music Help GLSetup Web UnrealEd; do
    [ -d "$A/$d" ] && ln -sfn "$A/$d" "$RUN/$d"
done

# Maps is a real directory of symlinks rather than one symlink to the tree, so locally-added test
# maps can sit alongside the stock ones without writing into shared assets.
mkdir -p "$RUN/Maps"
for m in "$A"/Maps/*; do [ -e "$m" ] && ln -sfn "$m" "$RUN/Maps/$(basename "$m")"; done
if [ -n "$EXTRA_MAPS" ]; then
    for extra in "$EXTRA_MAPS"/*.unr; do
        [ -e "$extra" ] && cp -f --remove-destination "$extra" "$RUN/Maps/"
    done
fi

# System is a real copy -- the engine rewrites UnrealTournament.ini and the log.
cp -a "$A/System" "$RUN/System"
mkdir -p "$RUN/Logs"

# The retail ini names WinDrv.WindowsClient as the viewport manager. Take the native config from the
# reference run tree when it is there; otherwise patch the one line.
if [ -f "$REF/System/UnrealTournament.ini" ]; then
    cp -f --remove-destination "$REF/System/UnrealTournament.ini" "$RUN/System/"
    [ -f "$REF/System/User.ini" ] && cp -f --remove-destination "$REF/System/User.ini" "$RUN/System/"
else
    sed -i -E 's#^ViewportManager=.*#ViewportManager=NSDLDrv.NSDLClient#' \
        "$RUN/System/UnrealTournament.ini"
fi

# Our build wins over whatever the asset tree happens to hold.
find "$BUILD" -name '*.so' -not -path '*_deps*' -exec \
    cp -f --remove-destination {} "$RUN/System/" \;
cp -f --remove-destination "$BUILD/UnrealTournament/UnrealTournament" "$RUN/System/"
chmod +x "$RUN/System/UnrealTournament"
# The bundled openal-soft (UE_BUNDLED_OPENAL) is loaded from beside the binary.
[ -f "$BUILD/UnrealTournament/libopenal.so.1" ] && \
    cp -f --remove-destination "$BUILD/UnrealTournament/libopenal.so.1" "$RUN/System/"

# Launcher, so a rebuilt tree is immediately runnable.
cp -f --remove-destination "$(dirname "$0")/run-template.sh" "$RUN/run.sh"
chmod +x "$RUN/run.sh"

echo "$RUN ready: $(ls "$RUN"/System/*.so 2>/dev/null | wc -l) modules from $BUILD"
