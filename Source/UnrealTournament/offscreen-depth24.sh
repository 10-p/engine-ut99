#!/bin/sh
# ufront 2.72 — 24-bit depth for Emscripten's emulated default framebuffer.
#
# Under -sOFFSCREEN_FRAMEBUFFER (the 2.43 present-path fix) the engine draws into a framebuffer Emscripten
# creates itself (libwebgl.js createOffscreenFramebuffer / resizeOffscreenFramebuffer), and that code
# allocates the depth attachment as GL_DEPTH_COMPONENT16 unconditionally — its own TODO says it never reads
# the context's depth/stencil attributes. Every render device that draws into framebuffer 0 (NOpenGLESDrv,
# UTGLR; not XOpenGL, which owns its FBO) therefore gets 16 bits of depth, and with the drivers' 1..65k
# frustum that is whole units of z at mid distance: surfaces trade places in bands as the player moves
# — the z-fight the owner reported on NOpenGLES. Rewrite the emitted loader so the renderbuffer is
# DEPTH_STENCIL (D24S8 on every desktop implementation; valid on WebGL 1 and 2) on the
# DEPTH_STENCIL_ATTACHMENT. The literals are Emscripten's GL enums in decimal, as -O2 emits them; the
# counts are asserted so a toolchain change fails the build loudly instead of silently shipping 16-bit
# depth again. Runs POST_BUILD on SDLLaunch.js (both the minified and the -g loader spell them the same,
# up to a space).
set -eu
f="$1"
[ -f "$f" ] || { echo "offscreen-depth24: no such file: $f" >&2; exit 1; }
sed -E -i \
  -e 's/renderbufferStorage\(36161, ?33189,/renderbufferStorage(36161, 34041,/g' \
  -e 's/framebufferRenderbuffer\(36160, ?36096, ?36161, ?context\.defaultDepthTarget/framebufferRenderbuffer(36160, 33306, 36161, context.defaultDepthTarget/g' \
  "$f"
s=$(grep -o 'renderbufferStorage(36161, 34041,' "$f" | wc -l)
a=$(grep -o 'framebufferRenderbuffer(36160, 33306, 36161, context.defaultDepthTarget' "$f" | wc -l)
if [ "$s" -ne 2 ] || [ "$a" -ne 1 ]; then
  echo "offscreen-depth24: expected 2 storage + 1 attachment rewrites in $f, found $s + $a — Emscripten's offscreen framebuffer code changed (libwebgl.js createOffscreenFramebuffer/resizeOffscreenFramebuffer); update the patterns" >&2
  exit 1
fi
echo "offscreen-depth24: $(basename "$f") — the emulated default framebuffer's depth is DEPTH_STENCIL (24+8), not DEPTH_COMPONENT16"
