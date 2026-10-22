# XOpenGLDrv on the web

XOpenGLDrv is the web build's first-choice render device (the launcher's order is XOpenGL, UTGLR, NOpenGLES; the
frontend lists it as `-XOPENGLRENDERER`). It needs WebGL 2.

## Context and link

- NSDLViewport gives XOpenGLDrv a WebGL 2 (GLES 3.0) context on the worker; `-WEBGL=1` does not apply to it.
- The launcher links with `-sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=1 -sGL_ENABLE_GET_PROC_ADDRESS=1` (the fork
  resolves its entry points through glad) and `-sFULL_ES2=1` (`Source/UnrealTournament/CMakeLists.txt`).

## The ES_WEBGL profile

The fork's `OpenGLVersion` setting (`[XOpenGLDrv.XOpenGLRenderDevice]`) has three values: `Core`, `ES` (desktop GLES
3.1, with shader interface blocks) and `ES_WEBGL` (the GLES 3.0 subset WebGL 2 has: plain varyings, `#version 300
es`). Under `__EMSCRIPTEN__` `Init` sets `ES_WEBGL`; that is the only place the platform picks the profile, and the
rest keys on the profile (`IsES()`, `IsWebGL()`, `OPT_GLES_WEBGL`). On desktop `OpenGLVersion=ES_WEBGL` runs the
same shaders natively.

## Textures

- WebGL exposes S3TC as `WEBGL_compressed_texture_s3tc`; the extension check accepts that name, so DXT1 uploads
  compressed.
- WebGL 2 rejects a compressed DXT1 mip whose base size is not a multiple of 4 (a 128x2 gradient, for example). On
  the `ES_WEBGL` profile, with S3TC, those mips are decoded to RGBA8 in software with the S3TC block decoder taken
  from gl4es (the fork's `Src/decompress.cpp`, by Benjamin Dobell and Matthäus Chajdas) and uploaded uncompressed.
- Without `WEBGL_compressed_texture_s3tc` (most mobile browsers), DXT1 mips are transcoded at upload to ASTC 4x4
  where the browser exposes `WEBGL_compressed_texture_astc`, else to ETC2 where it exposes
  `WEBGL_compressed_texture_etc`, else the engine keeps the P8 mips (`ENGINE_CHANGES.md`, "Textures"). The
  transcode pads every mip to whole 4x4 blocks, so it takes any size. Both encoders build for wasm with their
  scalar paths unless the web build enables wasm SIMD, so the ASTC transcode at a texture's first upload is slower
  there than natively.

## Antialiasing

XOpenGLDrv multisamples in framebuffers of its own (`UseAA`), so it does not use the engine's multisampled back
buffer (`msaa-backbuffer.js`).
