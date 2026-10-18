#!/usr/bin/env bash
# Compiles the NativesTest commandlet with OldUnreal 469's ucc, runs it under stock 469 and under this engine's
# native ucc, and diffs the "NT" lines: the five string natives 469 backported (Locs 238, Repl 239, $= 322,
# @= 323, -= 324) must give the same results. Prints IDENTICAL, or the diff and exits 1.
#
#   tools/oracle-469-natives/run.sh <469-install> <436-run-tree>
#     <469-install>   a stock OldUnreal 469 install (System64/ucc-bin). Its System dirs are copied to a scratch
#                     folder; the install itself is never written.
#     <436-run-tree>  a run-tree of this engine: System/ holds this build's ucc-bin (build-native/UCC/ucc-bin) and
#                     the stock packages. NativesTest.u is copied into it.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
src469=$(cd "$1" && pwd); run436=$(cd "$2" && pwd)
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT

# 469 needs a writable copy: `ucc make` writes the package and its ini beside the binary.
mkdir -p "$work/u469"
for e in "$src469"/*; do
    n=$(basename "$e")
    case "$n" in System|System64) cp -a "$e" "$work/u469/$n" ;; *) ln -s "$e" "$work/u469/$n" ;; esac
done
cp -r "$here/NativesTest" "$work/u469/"
for ini in "$work/u469/System/UnrealTournament.ini" "$work/u469/System64/UnrealTournament.ini"; do
    [ -f "$ini" ] || continue
    last=$(grep -n '^EditPackages=' "$ini" | tail -1 | cut -d: -f1)
    sed -i "${last}a EditPackages=NativesTest" "$ini"
done

# 469's compiler asks, per call, whether to keep a function "added in UT v451" (Repl, Locs): answer yes.
( cd "$work/u469/System64" && { yes y || true; } | HOME="$work/home" ./ucc-bin make -nohomedir ) > "$work/make.log" 2>&1 \
    || { cat "$work/make.log"; exit 1; }
( cd "$work/u469/System64" && HOME="$work/home" ./ucc-bin NativesTest.NativesTest -nohomedir ) 2>&1 \
    | grep -E '^NT ' > "$work/469.txt"

cp "$work/u469/System64/NativesTest.u" "$run436/System/"
( cd "$run436/System" && LD_LIBRARY_PATH=. ./ucc-bin NativesTest.NativesTest -nohomedir ) 2>&1 \
    | grep -E '^NT ' > "$work/436.txt"

if diff "$work/469.txt" "$work/436.txt"; then
    echo "IDENTICAL: $(wc -l < "$work/436.txt") lines"
else
    echo "DIFFERENT (469 left, 436 right)"; exit 1
fi
