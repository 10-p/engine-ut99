# XOpenGLDrv in engine-ut99

`XOpenGLDrv.XOpenGLRenderDevice` is OldUnreal's OpenGL 3.3 / GLES 3 renderer. The source is fetched from the fork
[10-p/XOpenGLDrv](https://github.com/10-p/XOpenGLDrv) (`XOPENGL_GIT_REPOSITORY`, `XOPENGL_GIT_TAG`, branch
`ut99-v400-compat`), which also carries its `XOpenGLDrv.int`; this folder holds the build glue (`CMakeLists.txt`) and
these notes. `BUILD_XOPENGLDRV` builds it (on in the native, Windows and web builds); `-XOPENGLRENDERER` selects
it. The web path: `WEB_INTEGRATION.md`.

## Build glue

- Compiles every fetched `Src/*.cpp` (a `file(GLOB)`: re-run the configure when the fork adds or removes a source),
  with the fork's own `Inc/` (its desktop-GL `glad.h`) and `glm/`, not the engine's `Thirdparty/`.
- Defines `UPACKAGE_NAME=XOpenGLDrv` (the static build's package symbols; it must match `IMPLEMENT_PACKAGE`, the `.int`
  and the ini's `XOpenGLDrv.XOpenGLRenderDevice`) and `SDLBUILD=1 SDL2BUILD=1 SDL3BUILD=0`, since the fork's header
  picks its SDL flavour from macros this engine does not define. It needs `USE_SDL`.

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
