#!/bin/sh
# 24-bit depth for Emscripten's emulated default framebuffer. Usage: offscreen-depth24.sh <SDLLaunch.js>
#
# Under -sOFFSCREEN_FRAMEBUFFER Emscripten allocates the depth of its framebuffer 0 (libwebgl.js
# createOffscreenFramebuffer / resizeOffscreenFramebuffer) as GL_DEPTH_COMPONENT16 whatever the context asks
# for, which z-fights at mid distance for the render devices that draw into it (NOpenGLESDrv, UTGLR; XOpenGL
# owns its FBO). This rewrites the emitted loader to a DEPTH_STENCIL renderbuffer (D24S8; valid on WebGL 1
# and 2) on the DEPTH_STENCIL_ATTACHMENT. The literals are GL enums in decimal, as the loader spells them; the
# rewrite counts are checked, so a toolchain change fails the build. Run POST_BUILD by the launcher target.
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
