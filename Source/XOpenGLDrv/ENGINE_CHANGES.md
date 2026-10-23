# XOpenGLDrv in engine-ut99

`XOpenGLDrv.XOpenGLRenderDevice` is OldUnreal's OpenGL 3.3 / GLES 3 renderer. The source is fetched from the fork
[10-p/XOpenGLDrv](https://github.com/10-p/XOpenGLDrv) (`XOPENGL_GIT_REPOSITORY`, `XOPENGL_GIT_TAG`, branch
`ufront`), which also carries its `XOpenGLDrv.int`; this folder holds the build glue (`CMakeLists.txt`) and
these notes. `BUILD_XOPENGLDRV` builds it (on in the native, Windows and web builds); `-XOPENGLRENDERER` selects
it. The web path: `WEB_INTEGRATION.md`.

## Build glue

- Compiles every fetched `Src/*.cpp` (a `file(GLOB)`: re-run the configure when the fork adds or removes a source),
  with the fork's own `Inc/` (its desktop-GL `glad.h`) and `glm/`, not the engine's `Thirdparty/`.
- Defines `UPACKAGE_NAME=XOpenGLDrv` (the static build's package symbols; it must match `IMPLEMENT_PACKAGE`, the `.int`
  and the ini's `XOpenGLDrv.XOpenGLRenderDevice`) and `SDLBUILD=1 SDL2BUILD=1 SDL3BUILD=0`, since the fork's header
  picks its SDL flavour from macros this engine does not define. It needs `USE_SDL`.
- Fetches etcpak and ARM astc-encoder at pinned commits and compiles only their codec sources into the device
  (etcpak's `ProcessRGB`, `Tables`, `Dither`; astc-encoder's `Source/astcenc_*.cpp`), on every target, with
  `XOPENGL_TRANSCODE=1`. Each picks its SIMD paths from the compiler's target macros (astc-encoder: NEON on arm64,
  SSE2 on x86_64, scalar on 32-bit ARM and wasm without SIMD).

## What the fork changes for this engine

The fork's version matrix keys on `ENGINE_VERSION` (`Core/Inc/UnObjVer.h` defines 400); most of its changes for
this engine are under `#if ENGINE_VERSION==400`:

- The device creates no GL context: it adopts the one NSDLViewport made current (on the threaded web build through
  `emscripten_webgl_get_current_context`, since SDL does not own it), `Unlock` does not swap and `Exit` does not
  delete it.
- v400's three-argument `DECLARE_CLASS` and package registration; `RenderPrivate.h` instead of a second `UnRender.h`;
  shims for what v400's Core lacks (`BOOL`, `PF_None`, `NAME_DevGraphics`, `appMsgf`, `FTime`, `GUglyHackFlags`); the
  UT469 texture-format and poly-flag names mapped to v400's values (`TEXF_BC1` is `TEXF_DXT1`; `PF_AlphaBlend` is 0,
  since 469's value is v400's `PF_CloudWavy`); `Viewport->SizeX/Y` for 469's physical size; the hardware-light
  gather reads v400's light fields.
- TCHAR is narrow on v400: format strings use `%s`, literals use `TEXT()`, and the shader dump logs each line
  directly (v400's `TArray<FString>::AddItem` assigns onto unconstructed memory).
- The `DistanceFogParams` uniform block is declared only with `OPT_DistanceFog`, so no shader carries an unbound block.
- `SetGamma` substitutes the retail defaults when the config loads zero: brightness 0.5 and `GammaMultiplier` 1.75.
- DXT1 is uploaded through `glCompressedTexImage2D` with its size computed directly (8 bytes per 4x4 block).
- `ReadPixels` reads the resolved frame when `UseAA` is on.
- On an OpenGL ES context (`IsES()`; Android) each batch fills the vertex and parameter buffers whole
  (`glBufferData`) and rotates them after every flush, as OldUnreal does on macOS and ARM Linux: those drivers copy
  a buffer, or wait, on a `glBufferSubData` into it while queued draws still read it. The web build keeps the
  partial updates: WebGL refuses a draw whose uniform buffer is smaller than the block.

## Textures

The engine hands a device the DXT1 copy of an S3TC texture only when the device sets `SupportsTC`, else its P8
mips; for the high-res S3TC set the P8 mips are a quarter to an eighth of the DXT1 size (`UTcrypt.nuwood`: 128x128
P8, 1024x1024 DXT1). `CheckExtensions` picks from the context's formats, the same way on every platform:

| The context has | DXT1 mips are | Bits a texel |
|---|---|---|
| S3TC (`GL_EXT_texture_compression_s3tc`, `WEBGL_compressed_texture_s3tc`) | uploaded as they are | 4 |
| ASTC (`GL_KHR_texture_compression_astc_ldr`, `WEBGL_compressed_texture_astc`) | transcoded to ASTC 4x4 at upload (astc-encoder, fastest preset) | 8 |
| ETC2 (an ES 3 context, desktop GL with `GL_ARB_ES3_compatibility`, `WEBGL_compressed_texture_etc`) | transcoded to ETC2 RGB at upload (etcpak) | 4 |
| none of them | not used: the engine keeps the P8 mips | 8 (palette) |

A transcode decodes each mip with the fork's gl4es block decoder (`Src/decompress.cpp`) and encodes it, at the
texture's first upload: in the precache after a level loads (`UsePrecache`, on by default; `PrecacheTexture` takes a
program for the upload, since the engine precaches between frames), else when it first comes into view. ASTC 4x4
holds a DXT1 block's four colours almost exactly, at twice the memory, and is decoded at 8-bit precision where the
context has `GL_EXT_texture_compression_astc_decode_mode`; ETC2 keeps the size but quantizes colour per 2x4
sub-block, which shows on dark, low-contrast surfaces. The ASTC encoder (`Src/ASTCEncoder.cpp`) shares a texture of
256x256 and up among one thread a core, up to 8; it is the slower of the two encoders, most without SIMD (32-bit
ARM, wasm). `Init` logs the choice (`XOpenGL: no S3TC: DXT1 textures ...`).
