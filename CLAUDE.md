# engine-ut99 — agent guide

**What this is:** the **private** engine repo for **UnrealTournament (1999), Unreal Engine 1 v400**, web
port. It holds engine source only and produces a **WASM artifact** (+ native binaries). It is **not** the
product on its own — the public monorepo `ufront` (`/home/ion/ufrontend`) consumes this engine's WASM
artifact, owns the host page / UI / build assembly, and owns the browser glue behind a thin **ABI shim**.

Created by **step 2.7** of the ufront re-architecture — the **second UE1 target** (after `engine-unreal1`,
the original Unreal 1998), riding the same shell + glue + ABI. Base import:
**[maximqaxd/ut99dc](https://github.com/maximqaxd/ut99dc)** (kept as the `upstream` remote; our private work
is fresh commits on `main`). ut99dc is a CMake, static-single-binary, SDL2/GLES2 modern port of the exact
UT99 v400 C++ engine — the hard fresh-toolchain work (ASM off, static package loader, POSIX platform layer,
struct-packing fix, socket shims) was already done there; **step 2.7 adds the web (Emscripten) layer**.

## Map of the world
| Thing | Path | Role |
|-------|------|------|
| This engine repo (private) | `/home/ion/engines/engine-ut99` | UT99 v400 source → WASM/native artifacts |
| The WASM template (sibling) | `/home/ion/engines/engine-unreal1` | the UE1-v200 web port — the pattern the web layer mirrors |
| Other sibling | `/home/ion/engines/engine-ut2004` | the UE2.5 port — origin of the exception-safe-loading patterns |
| Public monorepo | `/home/ion/ufrontend` | consumes the artifact; host/UI/glue/build; **migration state lives here** |
| Game assets (never in git) | `/home/ion/ut99-assets` | retail UT99 v400, `$UT99_ASSETS_DIR`; 16 packages + 64 maps |
| Base-import reference | [maximqaxd/ut99dc](https://github.com/maximqaxd/ut99dc) | Dreamcast/desktop UT99 port = the `upstream` remote |
| Android reference (ideas) | `/home/ion/UTA` | `Andiweli/UT99-Android` — vendors ut99dc; SDL2 glue ideas only |

> **Migration plan & durable state are in the monorepo**, not here:
> `/home/ion/ufrontend/docs/migration/STATE.md` (read first if continuing the migration),
> `ROADMAP.md`, `steps/2.7-engine-ut99.md` (this step's spec + decisions).

## Status (Phase 2, step 2.7 — IN PROGRESS)
- **2.7a DONE** — repo standup + **native-Linux build (the compat oracle)**. Native compiles clean on
  gcc-15 (`cmake --preset native`, 32-bit SDL2/GLES2) and the client **runs**: it loads the v400 engine and
  **all v68 retail packages + a real DM map** (`DM-Deck16][`, incl. BotPack/UnrealShare/UnrealI), proving
  v400↔v68 asset compatibility. (Headless it stops at the GL-context step — a real display/WebGL supplies
  that; the web E2E is the authoritative render gate.)
- **2.7b–2.7e + closeout** — WASM bare loader · FetchFS first-light menu · WebRTC net · full `targets/ut99`
  descriptor · real-engine boot+net E2E. Templated off engine-unreal1's web layer.

The engine bakes only the thin `webrtc_*` shim (`vendor/glue/webrtc-shim.js`, vendored from `@ufront/glue`
at `ABI_VERSION 1`); heavy glue (FetchFS + IDB + WebRTC session logic) is **injected at runtime** by the
host, not baked. The WASM build emits a **bare loader** + `meta.json` that the monorepo's `packages/build`
validates + assembles into `serve-web/`.

## Conventions / load-bearing facts (the UT99 specifics)
- **Version: UT99 v400, `PACKAGE_FILE_VERSION 68`** (`Source/Core/Inc/UnObjVer.h`; reads v60–68). The
  engine natively reads the retail v68 maps/packages — unlike engine-unreal1 (v200, ceiling ~v63), **there
  is no map-version ceiling here**.
- **Base = ut99dc.** CMake ≥3.15 / C++17, `-m32`, static single binary (`BUILD_STATIC`→`UNREAL_STATIC` +
  `--whole-archive` + `Core/…/UnStaticExports`), ASM off via `Core/Inc/UnGnuG.h`, POSIX platform in
  `Core/Src/UnUnix.cpp`, `-fpack-struct=4` (reflected class sizes), WinSock→BSD socket shims. **Do not
  re-derive these** — they are inherited from ut99dc.
- **Renderer (D-D):** default **`NOpenGLESDrv.NOpenGLESRenderDevice` (GLES2 → WebGL)** + windowing/input
  **`NSDLDrv.NSDLClient`** (SDL2). ut99dc ships **no software render *device*** (`Render/Src/UnSoftLn.cpp`
  is the rasterizer core, not a `URenderDevice`); a Null-render E2E discriminator is used instead. GLES2
  loads via `gladLoadGLES2Loader(SDL_GL_GetProcAddress)` (vendored `Thirdparty/glad_es`, glad 0.1.36).
- **Audio = null (`SoundDrv.NullAudioSubsystem`).** ut99dc's inis reference it but never shipped the
  package; `Source/SoundDrv/Null.cpp` (added step 2.7a) is the silent subsystem — matches engine-unreal1.
  ut99dc's `Audio` subsystem is OSS `/dev/dsp` (unusable on native-Linux/WASM); real SDL2/OpenAL audio is a
  documented **future** enhancement.
- **File layer is POSIX — untouched.** UT99 bottoms out at libc `fopen` / `FFileManagerLinux` +
  `opendir`/`readdir`; FetchFS (injected glue) intercepts **below** it at the Emscripten FS layer. **Do
  not** write an engine-side web file manager. `appBaseDir()` = `getcwd` (the launcher chdir's to `System`).
- **The web (WASM) layer mirrors engine-unreal1** (step 2.7b+): the `if(EMSCRIPTEN)` CMake branch, Asyncify
  main-loop inversion in `Source/UnrealTournament/Src/Launch.cpp`, exceptions-off with load failures
  resolving to **NULL** patched one site at a time, the Editor package linked (bot meshes/skins),
  GLog→console, and the engine-local `WebRTCDrv`. See `BUILD.md` for the per-site detail as it lands.

## Do NOT
- **Do NOT** push to `upstream` (it's maximqaxd/ut99dc). This repo stays **private, local-only**.
- **Do NOT** cherry-pick the Dreamcast/Android device hacks (KOSDrv/PVRDrv/AICADrv, `/pc/`/`/cd/` FATFS
  paths, `--wrap=fopen` file pool, LOW_MEMORY). Pull only what the web port genuinely needs.
- **Do NOT** bake heavy glue (FetchFS/IDB/WebRTC logic) into the WASM — only the thin `webrtc_*` shim.
- **Do NOT** change the ABI. It's frozen at `vendor/glue/ue_abi.h` (`ABI_VERSION 1`); a change is a
  deliberate cross-engine act owned by the monorepo.
- **Do NOT** commit game assets or build trees (`build-*/`) — gitignored.
