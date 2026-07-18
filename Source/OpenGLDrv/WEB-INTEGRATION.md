# Integrating UTGLR (`OpenGLDrv`) into the UT99 web build — instructions for the ufront/web agent

**Audience:** the agent integrating engine artifacts into the `ufront` monorepo (host page, build
assembly, `targets/ut99`). This document is written by the engine-side (UTGLR port) author; it does **not**
modify the monorepo — it tells you exactly what to change there.

## TL;DR

UTGLR is now a **drop-in alternative web renderer** for UT99: package **`OpenGLDrv`**, class
**`OpenGLRenderDevice`**, gl4es-backed (legacy desktop-GL → GLES2 → WebGL). Integration is **build-flag +
one `target.json` entry + doc text** — **no engine-source changes, no ABI change, no glue change.**

## No engine / ABI / glue changes are required (confirmed)

The entire engine-side change set is:
- new package `Source/OpenGLDrv/` (UTGLR ported to UE1 v400), and
- additive, gated edits to `Source/CMakeLists.txt` only (a `USE_GL4ES` option, the gl4es `FetchContent`
  block, and — inside the existing `EMSCRIPTEN` branch — enabling `BUILD_NOPENGLDRV` iff `USE_GL4ES`).

**Untouched:** `Core/Engine/Render/NSDLDrv/IpDrv/Fire/SoundDrv/UnrealTournament/Launch`, the frozen ABI
(`vendor/glue/ue_abi.h`, v1), the vendored `webrtc-shim.js`, and all injected glue (FetchFS/IDB/WebRTC).
gl4es is compiled **statically into the wasm** — it is not glue, needs no runtime injection, and does not
change the artifact contract or `meta.json` format. NOpenGLESDrv still builds and remains the default.

## Prerequisite: land the driver in the engine tree you build from

The OpenGLDrv package + `Source/CMakeLists.txt` edits currently live in **`engine-ut99-utglr`** (branch
`step/2.7d-webrtc-net`). Make sure they're present in whichever `engine-ut99` tree your artifact build uses
before Step 1.

## Step 1 — Build the engine wasm artifact with UTGLR + gl4es

```bash
cd <engine-ut99>/Source
source ~/emsdk/emsdk_env.sh
cmake --preset wasm-perf -DUSE_GL4ES=ON [-DGL4ES_SOURCE_DIR=/path/to/gl4es] \
  && cmake --build --preset wasm-perf -j"$(nproc)"
# -> build-wasm-perf/SDLLaunch/SDLLaunch.{js,wasm,data} + meta.json  (gl4es baked in; ~4.4 MB wasm)
```
- Consume it **exactly as today**: `yarn build --target ut99 --engine-artifact <…/build-wasm-perf/SDLLaunch>`.
  The artifact filenames, `meta.json` schema (abi 1, `wasm-integrated`), and the build validator are
  unchanged.
- **gl4es source is configurable:** default is vanilla `ptitSeb/gl4es` via git (`GL4ES_GIT_REPOSITORY` /
  `GL4ES_GIT_TAG`); pass `-DGL4ES_SOURCE_DIR=<path>` to use a local checkout instead.
- **Opt-in:** without `-DUSE_GL4ES=ON`, the web build stays NOpenGLESDrv (bit-identical to today). So this
  change is entirely additive.

## Step 2 — Select `OpenGLDrv` as the web renderer

In `targets/ut99/target.json`, set the `videoDrivers` entry (currently the parallel **XOpenGLDrv** effort's
device — decide which renderer the ut99 web target ships, or offer both):
```json
"videoDrivers": [
  { "device": "OpenGLDrv.OpenGLRenderDevice", "args": "" }
]
```
Why the name matters: NSDLDrv opens a GL window only if the device name contains `"OPENGL"` (it does), and
under Emscripten forces an ES2/WebGL context — which gl4es targets. No NSDLDrv change needed.

## Step 3 — Update renderer-mention docs (cosmetic)

`targets/ut99/README.md`, `targets/ut99/overlay/README.md`, `targets/ut99/branding/help.md` say
"NOpenGLESDrv" — change to "OpenGLDrv (UTGLR, via gl4es)".

## Verify in the browser E2E (the authoritative render gate — engine-side only proved native)

- **First light:** boot the intro (`CityIntro`) and a DM map; expect richer output than NOpenGLESDrv
  (proper detail textures, fog, multitexture).
- **S3TC / DXT1:** UT99's S3TC texture pack (in the retail assets) uploads compressed **iff** the WebGL
  context exposes `WEBGL_compressed_texture_s3tc` (most desktop browsers do). If absent, the engine
  decompresses DXT1→RGBA — a safe fallback. Watch for GL errors on **non-4-aligned DXT1 mips** (e.g. a
  128×2); gl4es' own DXT decode should handle them.
- **Keep off on web:** `UseFragmentProgram=0`, `UsePalette=0` (ARB assembly programs & paletted textures
  aren't supported on GLES2/gl4es; UTGLR's fixed-function + RGBA fallbacks are complete).

## For Unreal 1 (v200) — not yet

UTGLR is ported to **UT99 (v400) only**. A v200 port is future work; the same `UTGLR_ENGINE_UT99DC`-style
gating pattern applies. See `Source/OpenGLDrv/PORTING.md` → "Notes for the future Unreal-v200 port".

## Full engine-side detail

`Source/OpenGLDrv/PORTING.md` — the complete port (adaptation catalogue, gl4es wiring, the S3TC fix, the
native headless test harness, known issues).
