# Porting UTGLR to engine-ut99 (UE1 v400) — status & handoff

**What this is.** `Source/OpenGLDrv/` is [UTGLR](https://www.cwdohnal.com/utglr/) — Chris Dohnal's mature
legacy-OpenGL UE1 render device (`OpenGLDrv.OpenGLRenderDevice`, written for **UT v436**) — ported to this
engine (**ut99dc / UE1 v400**) and wired to run on **WebGL** via the **gl4es** (desktop-GL→GLES2)
translator, mirroring `engine-ut2004/GL4ESDrv`.

**Base source:** the 2010-era UTGLR drop at `/home/ion/utglr/` (`OpenGL.cpp/.h`, `OpenGL1Funcs.h`,
`OpenGLExtFuncs.h`, `OpenGLDrv.cpp/.h`, `c_gclip.*`, `c_rbtree.h`). Win32-only files (`wglext.h`, `.sln`,
`.vcproj`, `glext.h`, `UnFile.h`) were not copied.

## Status (this pass)

| Milestone | State | Evidence |
|-----------|-------|----------|
| **M1** compile natively | ✅ | `OpenGLDrv.so` builds clean (0 errors); package/class symbols exported |
| **M2** render via system desktop GL | ✅ | DM-Codex, ~26k frames headless on the RTX 3090, no GL errors |
| **M3** render through gl4es→GLES2 | ✅ | `GL_VERSION: 2.1 gl4es wrapper 1.1.7`, hundreds of frames, no missing procs |
| **M4** wasm/WebGL build | ✅ | `wasm` + `wasm-perf` link UTGLR+gl4es → `SDLLaunch.{js,wasm,data}` + `meta.json` (0 errors); in-browser render/E2E is the `ufront` monorepo's gate |

## The prime directive was honored: **zero engine-source edits**

Nothing under `Core/ Engine/ Render/ NSDLDrv/ IpDrv/ Fire/ SoundDrv/ UnrealTournament/` was modified. The
complete change set outside the new package is **additive, gated edits to `Source/CMakeLists.txt`** (the
`USE_GL4ES` option, the gl4es `FetchContent` block, and — inside the existing `EMSCRIPTEN` branch —
enabling `BUILD_NOPENGLDRV` iff `USE_GL4ES`). Everything else is the new `Source/OpenGLDrv/` package.

Two design choices made this possible:

1. **Reused the reserved build slot.** `Source/CMakeLists.txt` already had
   `if(BUILD_NOPENGLDRV) → add_subdirectory(OpenGLDrv)` + the `--whole-archive`/INSTALL_TARGETS plumbing.
   Creating `Source/OpenGLDrv/` + `-DBUILD_NOPENGLDRV=ON` needs no other shared-file edit for native.

2. **Named the package to satisfy NSDLDrv's context logic — LOAD-BEARING.**
   `Source/NSDLDrv/Src/NSDLViewport.cpp:297` opens a GL window **iff the render-device name contains the
   substring `"OPENGL"`**, and on native leaves the profile at desktop **COMPATIBILITY** unless the name
   also contains `"GLES"` (the `#ifdef __EMSCRIPTEN__` branch forces ES2 for WebGL). The name
   **`OpenGLDrv.OpenGLRenderDevice`** (UTGLR's own upstream name) therefore gets a desktop-GL context
   natively and a WebGL/ES2 context under Emscripten **with no NSDLDrv edit**. ⚠️ If you rename the
   package/class, keep `"OpenGL"` in it and keep `"GLES"` out — otherwise NSDLDrv won't give it a GL window.

## Adaptation catalogue (all gated by `UTGLR_ENGINE_UT99DC`, defined in the package CMake)

`UTGLR_ENGINE_UT99DC` guards every deviation from upstream UTGLR (grep it). `UTGLR_UT_BUILD` and `__UNIX__`
are also defined for this target.

| Site | Upstream (v436/Win32) | This port (v400/Linux) | Why |
|------|-----------------------|------------------------|-----|
| `OpenGL.h:~359` | 4-arg `DECLARE_CLASS(...,OpenGLDrv)` | 3-arg `DECLARE_CLASS(...,CLASS_Config)` | v400's macro is 3-arg; package comes from `IMPLEMENT_PACKAGE`/`THIS_PACKAGE` |
| `OpenGL.h:~637`, `OpenGL.cpp:~3665` | `FTime m_prevFrameTimestamp` / local | `FLOAT` | this engine has no `FTime`; frame limiter stores `appSeconds()` |
| `OpenGL.h:36` | `#include <tchar.h>` | gated out on `__UNIX__` | Linux/gcc has no `<tchar.h>` |
| `OpenGL.cpp:~5850/5859` | `dbgStr[0] = _T('\0')` | `= 0` | avoid `_T`; ANSI TCHAR here |
| `OpenGL.cpp:52` | (engine headers first) | `#include <new>/<map>/<vector>` **before** `OpenGLDrv.h` | front-load STL before Core: (a) `<new>` first → Core's inline `operator delete` matches `<new>`'s noexcept (C++11+); (b) on **Emscripten libc++**, `<map>` pulls `<time.h>` **before** Core/UnFile.h's `#define clock(Timer)` mangles `clock_t clock(void)` |
| `OpenGLDrv.h:~30` | SDL 1.2 (`<SDL/SDL.h>`) | `"SDL2/SDL.h"` + inline compat shims | `SDL_GL_SwapBuffers`/`SDL_SetGammaRamp`/`SDL_ListModes`/`SDL_FULLSCREEN` were removed in SDL2; NSDLDrv owns presentation/window |
| `OpenGL.cpp:~2911` (`Init`) | `SDL_GL_LoadLibrary("libGL.so.1")` (fatal on fail) | skipped | NSDLDrv already created+made-current the context and loaded GL before Init; loading again → fatal "already loaded". Matches NOpenGLESDrv. |

No `-fpack-struct=4` / calling-convention work is needed per-package — `STDCALL`/`FASTCALL`/`DLL_EXPORT` are
empty on gcc (`Core/Inc/UnGnuG.h`), the MSVC `__asm`/`__declspec(naked)`/SSE paths are `_WIN32`-gated off
(C fallbacks active), `UTGLR_RUNE_BUILD` methods are compiled out, and `__INTEL_BYTE_ORDER__` is defined by
the engine.

## gl4es integration (WebGL path), gated by `USE_GL4ES` / `UTGLR_USE_GL4ES`

- **`Source/CMakeLists.txt`** (additive, gated): `option(USE_GL4ES … OFF)` + a `FetchContent` block that,
  when `USE_GL4ES OR EMSCRIPTEN`, builds gl4es with `NOX11 NOEGL STATICLIB DEFAULT_ES=2
  NO_INIT_CONSTRUCTOR`, redirects its archive output per build dir, and exposes `gl4es::GL`. `-m32` is
  inherited from the native preset's `CMAKE_C_FLAGS`. **The gl4es source is configurable** — set
  `-DGL4ES_SOURCE_DIR=<path>` to use a local checkout (vanilla or fork); leave it empty (default) to
  fetch vanilla upstream `GL4ES_GIT_REPOSITORY` (`https://github.com/ptitSeb/gl4es.git`) at
  `GL4ES_GIT_TAG` (default `master` — pin for reproducibility). Nothing is hardcoded to a personal fork.
- **`Source/OpenGLDrv/CMakeLists.txt`**: when `USE_GL4ES`, include `${gl4es_SOURCE_DIR}/include` (BEFORE
  system, so `<GL/gl.h>` is gl4es'), link `gl4es::GL` **instead of** system GL, define `UTGLR_USE_GL4ES`.
- **`OpenGL.cpp`**: under `UTGLR_USE_GL4ES`, `Init` does `setenv(LIBGL_ES=2/…)`,
  `set_getprocaddress(SDL_GL_GetProcAddress)`, `initialize_gl4es()`; and `GetGL1Proc`/`GetGLExtProc` resolve
  via **`gl4es_GetProcAddress`** (so calls hit gl4es' translation, not system GL). Mirrors
  `engine-ut2004/GL4ESDrv/Src/GL4ESRenderDevice.cpp`.

**Native gl4es nuance:** on native, NSDLDrv hands OpenGLDrv a desktop-**compat** context (name lacks
`"GLES"`), so gl4es runs its GLES2 path atop NVIDIA's compat driver. This validates the CMake/link/init +
translation mechanics (the WebGL de-risking goal). Strict-GLES2/WebGL conformance is validated later by the
monorepo's in-browser E2E, where the context really is WebGL/ES2.

## Config policy

Committed engine config is **unchanged** (default renderer stays `NOpenGLESDrv`; `Config/System/*.ini`
untouched). Select UTGLR by setting `GameRenderDevice`/`WindowedRenderDevice`/`RenderDevice =
OpenGLDrv.OpenGLRenderDevice` in the **run tree / monorepo assembly**, not here. The retail
`[OpenGLDrv.OpenGLRenderDevice]` ini section already present in `Default.ini` applies as-is.

## S3TC / DXT1 compressed textures

This engine (v400) has exactly one compressed format: **`TEXF_DXT1` (S3TC)** — no DXT3/DXT5 (those are
227/469 only). The engine reads `URenderDevice::SupportsTC` to decide delivery: if the device advertises
**`SupportsTC=0`, the engine silently decompresses DXT1 → RGBA7 before the driver ever sees it** (so the
driver can't upload compressed even if it wanted to).

Verified against the retail S3TC texture pack in `$UT99_ASSETS_DIR` (~850 MB install): **CityIntro loads
38 DXT1 textures** (`city.utx` — Alley/Alley2/Grate3/…); the rest are P8/RGBA7. DM/CTF maps use *no* DXT1
(all P8/RGBA7), so they're a poor S3TC test — use **CityIntro** (the default intro).

**Fix applied (gated `UTGLR_ENGINE_UT99DC`, `SetRes`).** UTGLR defaults `UseS3TC=1` for the UT build, but
its per-param bit-offset config trick doesn't carry that default to the render-device instance in this
engine (it resolved to 0), so `SupportsTC` was 0 and the engine decompressed every DXT1 texture. `SetRes`
now reads `UseS3TC` **directly from the ini with default-ON** (`GConfig->GetBool`), still honoring an
explicit `UseS3TC=False` opt-out. Result: DXT1 delivered raw and uploaded as
`GL_COMPRESSED_RGBA_S3TC_DXT1_EXT`.

Per-backend (all verified except the last):
- **Native desktop GL** — S3TC used: 38 compressed uploads on CityIntro, 0 GL errors. ✅
- **Native gl4es** — gl4es passes through the underlying NVIDIA s3tc; DXT1 uploaded compressed, 0 errors. ✅
- **Browser WebGL** — depends on the browser exposing `WEBGL_compressed_texture_s3tc`. Present → gl4es
  advertises it, DXT1 uploads compressed; absent → `SupportsTC` gated to 0 (line ~2254) and the engine
  decompresses to RGBA (correct, safe fallback). Validated in the `ufront` E2E. (XOpenGL-effort edge case:
  WebGL2 rejects `glCompressedTexImage2D` for non-4-aligned DXT1 mips, e.g. 128×2; gl4es' own DXT decode
  covers those.)

## Build & run

```bash
cd Source
# M2 — native desktop-GL oracle:
cmake --preset native -DBUILD_NOPENGLDRV=ON               && cmake --build --preset native -j"$(nproc)"
# M3 — native through gl4es (desktop GL -> GLES2):
cmake --preset native -DBUILD_NOPENGLDRV=ON -DUSE_GL4ES=ON && cmake --build --preset native -j"$(nproc)"
#   (add -DGL4ES_SOURCE_DIR=/path/to/gl4es to use a local checkout instead of fetching vanilla)
# M4 — wasm/WebGL (product = wasm-perf, what ufront consumes; dev = wasm). USE_GL4ES auto-enables OpenGLDrv:
source ~/emsdk/emsdk_env.sh
cmake --preset wasm-perf -DUSE_GL4ES=ON [-DGL4ES_SOURCE_DIR=/path/to/gl4es] && cmake --build --preset wasm-perf -j"$(nproc)"
#   -> build-wasm-perf/SDLLaunch/SDLLaunch.{js,wasm,data} + meta.json
```
**gl4es is optional** (`USE_GL4ES` defaults OFF). The desktop-GL backend (M2) builds UTGLR standalone
against system OpenGL and is useful on native UT400 / Unreal-v200 for features WebGL/GLES2 lack — notably
**S3TC compressed textures**, anisotropy, LOD bias. gl4es is only pulled in for the WebGL target (or the
optional native gl4es validation).

Run headless (see the [ut99-native-headless-render-harness] memory): from a **private** run tree (never
`/tmp/ut99-play` — a parallel `engine-ut99-xopengl` effort clobbers it), with the built `.so`s + client +
an ini selecting `OpenGLDrv.OpenGLRenderDevice`:
`SDL_VIDEODRIVER=offscreen ./UnrealTournament DM-Codex -nosound`. The driver logs a throttled heartbeat
`OpenGLDrv(UTGLR): Unlock N (Blit=1)` every 60 frames (mirrors NOpenGLESDrv; a debug aid — safe to remove).

## Known issues / not-done

- **Shutdown segfault (pre-existing, not this port).** On quit/SIGINT under the headless offscreen harness,
  the client segfaults in `RenDev->Exit()→UnsetRes()→Flush()→glDeleteTextures()` because the offscreen/EGL
  teardown unmaps libGL before the render device's Exit. **NOpenGLESDrv segfaults identically** (exit 139),
  so it's an engine/harness teardown-order issue, not a driver bug. Does not affect rendering. A real fix
  is an engine/harness teardown-order change (out of scope for driver-only); the web path (Asyncify, no
  SIGINT) likely won't hit it.
- **Benign warning:** Core's inline `operator delete` (`UnFile.h`) triggers a "different exception
  specifier" *warning* against `<new>`; harmless, contained to `OpenGL.cpp`.
- **ARB assembly shaders / paletted textures**: `UseFragmentProgram` and `UsePalette` should stay off for
  the WebGL/gl4es path (unsupported by GLES2/gl4es); UTGLR's fixed-function + RGBA fallbacks are complete.
  **S3TC now works** natively and through gl4es — see the S3TC section above.
- **M4 (wasm/WebGL): done at the build level.** Inside the `EMSCRIPTEN` branch, `BUILD_NOPENGLDRV` is now
  enabled iff `USE_GL4ES`, so `cmake --preset wasm[-perf] -DUSE_GL4ES=ON` links UTGLR+gl4es into
  `SDLLaunch.{js,wasm,data}` + `meta.json` (verified 0 errors; product wasm ≈4.4 MB, dev ≈17 MB with
  gl4es). The one Emscripten-specific source fix was the `<map>`→`<time.h>` front-load above (libc++
  only). The actual **in-browser WebGL render/E2E is owned by the `ufront` monorepo** (host page, FetchFS/
  glue injection, renderer selection). On web, select the renderer via `GameRenderDevice=
  OpenGLDrv.OpenGLRenderDevice` in the assembled config.

## Notes for the future Unreal-v200 port

- The **NSDLDrv naming reliance** above is the biggest portability hinge — v200 may lack NSDLDrv or detect
  GL windows differently.
- Re-check the two type assumptions that differed from v436: **`DECLARE_CLASS` arity** and **absence of
  `FTime`**. If v200 differs again, extend the same `UTGLR_ENGINE_UT99DC`-style gate (or add a `_V200` one).
- Everything is gated so upstream UTGLR and a v200 flavor can coexist; no engine edits to rebase.
