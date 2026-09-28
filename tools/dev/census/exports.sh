#!/usr/bin/env bash
# exports.sh <run-tree> <out-dir> -- a cross-width ORACLE for property layout (ufront 2.83). Runs the ucc
# batchexport commandlets; run it in a 32-bit and a 64-bit tree and `diff -r` the two out-dirs. The level
# export prints every property of every actor in a map, read at the engine's offsets, so a width
# disagreement becomes a text difference. 2.83: level/polys/pcx/bmp/wav/music byte-identical at 32 and 64;
# of 496 exported classes only two differed, both by config values the trees had saved (a class's exported
# defaults include its config), so compare trees with the same ini.
set -u
TREE=$1; OUT=$2
rm -rf "$OUT"; mkdir -p "$OUT"/{level,polys,classes,pcx,bmp,wav,music}
cd "$TREE/System"
BAD='appError|Critical: |Assertion failed|SIGSEGV|General protection|ERROR: AddressSanitizer'
run() { local name=$1; shift; timeout 600 ./ucc-bin "$@" -nohomedir > "$OUT/$name.log" 2>&1
        echo "$name: exit=$? crash-lines=$(grep -caE "$BAD" "$OUT/$name.log")"; }
run level   batchexport "../Maps/DM-Deck16][.unr" Level t3d "$OUT/level"
run polys   batchexport "../Maps/DM-Deck16][.unr" Polys t3d "$OUT/polys"
run classes batchexport ../System/BotPack.u Class uc "$OUT/classes"      # file name is case-sensitive: BotPack
run pcx     batchexport ../Textures/GenFX.utx Texture pcx "$OUT/pcx"
run bmp     batchexport ../Textures/GenFX.utx Texture bmp "$OUT/bmp"
run wav     batchexport ../Sounds/Announcer.uax Sound wav "$OUT/wav"
run music   batchexport ../Music/Botmca9.umx Music s3m "$OUT/music"
for d in level polys classes pcx bmp wav music; do echo "  $d: $(ls "$OUT/$d" | wc -l) files, $(du -sb "$OUT/$d" | cut -f1) bytes"; done
