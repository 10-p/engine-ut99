# OpenGLDrv (UTGLR) in engine-ut99

`OpenGLDrv.OpenGLRenderDevice` is [UTGLR](https://www.cwdohnal.com/utglr/), Chris Dohnal's legacy-OpenGL render
device for UT v436, on Daniel Vogel's (Loki) OpenGLDrv. The source is fetched from the fork
[10-p/utglr-copy](https://github.com/10-p/utglr-copy) (`UTGLR_GIT_REPOSITORY`, `UTGLR_GIT_TAG`, branch `main`); this
folder holds the build glue (`CMakeLists.txt`) and these notes. `BUILD_UTGLR` builds it on native Linux and Windows,
`USE_GL4ES` (on for the web) builds it against gl4es. `-UTGLRRENDERER` selects it. The web path:
`WEB-INTEGRATION.md`.

## Build glue

- Compiles the fetched `OpenGL.cpp`, `OpenGLDrv.cpp` and `c_gclip.cpp` with `UTGLR_UT_BUILD`, `UTGLR_ENGINE_UT99DC`
  and `__UNIX__` on every target (Windows included), and links Render, Engine, Core and SDL2. It needs `USE_SDL`.
- Links no GL library: every GL call goes through UTGLR's function-pointer table, resolved with
  `SDL_GL_GetProcAddress` against the context NSDLDrv made current (the system `opengl32`'s on Windows).
- With `USE_GL4ES`: gl4es's `include/` comes before the system headers, `gl4es::GL` (the top-level
  `Source/CMakeLists.txt` fetches and builds it) supplies the GL symbols, and `UTGLR_USE_GL4ES` is defined.

## What the fork changes for this engine

`UTGLR_ENGINE_UT99DC` marks each change (grep it in the fork):

- v400's three-argument `DECLARE_CLASS`; no `FTime` (the frame limiter keeps `appSeconds()` in a `FLOAT`).
- SDL2 instead of SDL 1.2 on every platform, with compatibility shims for the calls SDL2 removed. NSDLDrv owns the
  window and the context, so `Init` loads no GL library and the WGL path is off on Windows.
- No `<tchar.h>`; the STL headers are included before Core's, ahead of Core's `operator delete` and `clock` macro.
- `UseS3TC` is read from the ini with a default of on, so DXT1 textures reach the device compressed
  (`SupportsTC`); `UseS3TC=False` turns it off, and the engine then decompresses DXT1 before the device sees it.

## Context

NSDLDrv opens a GL window when the render device's name contains `OPENGL`; natively every GL device gets a
compatibility-profile context. Keep `OpenGL` in the package or class name. Settings are the
`[OpenGLDrv.OpenGLRenderDevice]` section the retail ini already has.
