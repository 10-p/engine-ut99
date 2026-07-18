# XOpenGLDrv — Web (WebGL2) integration guide

Handoff for the agent wiring the new renderer(s) into the UT99 **web (Emscripten/WASM)** build. This is the
"how to build, assemble, and test the WebGL build" companion to:

- **`Source/XOpenGLDrv/ENGINE_CHANGES.md`** — every engine-tree edit + driver adaptation, with rationale. Read
  this for the *why* and the exact gated diffs.
- **`BUILD.md`** (repo root) — the CMake preset matrix (native / `wasm` / `wasm-perf`) and the native oracle.

The port is **driver-only + build-glue**: all v400 adaptations live in `Source/XOpenGLDrv/` gated
`#if ENGINE_VERSION==400`; the only engine-tree edits are 4 build-wiring points, all gated on the
`WASM_USE_XOPENGL` CMake option (default **OFF** → the existing NOpenGLESDrv WebGL1 build is byte-for-byte
unchanged).

---

## 1. Build the WASM artifact

```bash
source ~/emsdk/emsdk_env.sh                      # sets $EMSDK for the toolchain file
cd /home/ion/engines/engine-ut99-xopengl/Source

# dev (assertions/debug) -> build-wasm/
cmake --preset wasm      -DWASM_USE_XOPENGL=ON
cmake --build --preset wasm      -j"$(nproc)"    # or: cd ../build-wasm && make UnrealTournament -j$(nproc)

# product (-O3, what ufront consumes) -> build-wasm-perf/
cmake --preset wasm-perf -DWASM_USE_XOPENGL=ON
cmake --build --preset wasm-perf -j"$(nproc)"
```

**`-DWASM_USE_XOPENGL=ON` is the switch.** Without it the WASM build uses NOpenGLESDrv (WebGL1/ES2). With it,
XOpenGLDrv is built as the *sole* WASM renderer on WebGL2/ES3.

Artifact (bare loader): `build-wasm[-perf]/SDLLaunch/SDLLaunch.{js,wasm,data}` + `meta.json` (ABI 1).

> **CMake GLOB gotcha (bit me):** `Source/XOpenGLDrv/CMakeLists.txt` uses `file(GLOB Src/*.cpp)` *without*
> `CONFIGURE_DEPENDS`. After adding/removing any `Src/*.cpp` (or `Inc/*.h` that changes the file set) you
> **must re-run `cmake .` in the build dir** before `make`, or the new file isn't compiled — the WASM static
> link then fails with `undefined symbol`, and a native `.so` links silently *without* it.

## 2. The 4 engine-tree wiring points (all `WASM_USE_XOPENGL`-gated)

This is the generalizable pattern for wiring **any** new WebGL render device (applies to a WebGL UTGLR too —
just swap the package/class names and use a distinct option like `WASM_USE_UTGLR`). Full diffs + rationale in
`ENGINE_CHANGES.md §1–2`.

| # | File | What |
|---|------|------|
| 1 | `Source/CMakeLists.txt` | `option(WASM_USE_XOPENGL … OFF)` (declared **before** the EMSCRIPTEN branch); `add_compile_definitions(WASM_USE_XOPENGL)` in that branch; renderer selection — build XOpenGLDrv (not NOpenGLESDrv) as the sole WASM renderer when ON. NOpenGLESDrv & XOpenGLDrv are mutually exclusive on WASM (both vendor a `glad` → duplicate globals under `--whole-archive`). |
| 2 | `Source/UnrealTournament/CMakeLists.txt` | WebGL2/GLES3 link flags when ON: `-sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sFULL_ES3=1 -sGL_ENABLE_GET_PROC_ADDRESS=1` (else the WebGL1 `-sFULL_ES2=1`). `GL_ENABLE_GET_PROC_ADDRESS` is required for glad's function-pointer loading. |
| 3 | `Source/NSDLDrv/Src/NSDLViewport.cpp` | Under `__EMSCRIPTEN__ && WASM_USE_XOPENGL`, request an **ES 3.0** WebGL2 context (major=3,minor=0) instead of ES 2.0. |
| 4 | `Source/UnrealTournament/Src/Launch.cpp` | Under `WASM_USE_XOPENGL`, the emscripten boot forces `GameRenderDevice/WindowedRenderDevice/RenderDevice = XOpenGLDrv.XOpenGLRenderDevice` (the shipped Default.ini is Dreamcast-configured, so the web renderer is chosen in code here). |

Plus the driver package itself: **`Source/XOpenGLDrv/`** (registered via `add_subdirectory` in point 1).

## 3. Assemble `serve-web/` in the monorepo (`ufront`)

The engine only emits the bare loader; `ufront` (`/home/ion/ufrontend`) assembles the host page + glue +
assets. Point its build at **this** repo's artifact (the default `--engine-artifact` points at
`engine-ut99/build-wasm-perf`, which is the *other* engine — you must override it):

```bash
cd /home/ion/ufrontend
yarn build --target ut99 \
  --engine-artifact /home/ion/engines/engine-ut99-xopengl/build-wasm-perf/SDLLaunch \
  --assets          /home/ion/ut99-assets
```

A correct UT99 assembly logs: `9 symlinks: GLSetup,Help,Maps,Music,Sounds,System,Textures,UnrealEd,Web`.
`targets/ut99/target.json` already has `videoDrivers: [{ device: "XOpenGLDrv.XOpenGLRenderDevice" }]`.

For a fast iterate loop you can just `cp SDLLaunch.{js,wasm,data} meta.json` from the build dir into
`serve-web/` over an already-assembled tree, then serve `python3 -m http.server 8080 --directory serve-web`.

> **serve-web is SHARED + regenerable across targets.** Another session assembling for a different target
> (e.g. ut2004) repoints its asset symlinks and manifest, so the UT99 engine then streams the wrong packages
> and **aborts at `Core.u` with `Can't find Class Core.PointerProperty`** (a UT2004-era class — the tell). Fix
> = re-run the `yarn build --target ut99 …` above. If a boot regresses early, `ls -la serve-web/` and check the
> symlinks point at `ut99-assets`, not `ut2004-assets`.

## 4. GL profile selection (`OpenGLVersion` ini enum: Core / ES / ES_WEBGL)

XOpenGLDrv's `OpenGLVersion` is a `CPF_Config` enum: `Core`, `ES` (desktop GLES 3.1, upstream io-blocks), and
**`ES_WEBGL`** (WebGL2 == GLES 3.0 subset: flattened varyings, `#version 300 es`, S3TC via the WebGL ext name).

- **On web you don't set anything** — Init forces `OpenGLVersion=GL_ES_WEBGL` under `__EMSCRIPTEN__` (a browser
  can only give a WebGL2 context). This is the single platform decision; everything else keys off the runtime
  profile via `IsES()` / `IsWebGL()` (see `ENGINE_CHANGES.md` "GL profile selection").
- **On desktop you can set `OpenGLVersion=ES_WEBGL`** in `[XOpenGLDrv.XOpenGLRenderDevice]` to validate the
  exact WebGL code path natively (no browser needed).

## 5. Testing

- Bring-up probe (ANGLE/RTX via Playwright, dumps console + MEMFS log + screenshot to `/tmp/xog_web*`):
  `cd /home/ion/ufrontend/packages/e2e && XDG_RUNTIME_DIR=/run/user/1000 PROBE_MS=45000 node xog-probe.cjs`
  Green boot = `Bringing Level CityIntro.MyLevel up for play` in the engine log, no `Failed compiling` /
  `null function` / `abort` / `GL_INVALID` in the console. Judge pixels from the screenshot (a bright
  CityIntro frame is ~97% non-black).
- The repo's real E2E is `packages/e2e/ufront-e2e.mjs` + `ut99-wasm-boot.spec.js`.

## 6. S3TC / DXT1 on WebGL (already handled — reference if wiring another renderer)

- ANGLE exposes S3TC as **`WEBGL_compressed_texture_s3tc`**, *not* the desktop `GL_EXT_texture_compression_s3tc`
  (emscripten also mirrors `GL_WEBGL_compressed_texture_s3tc`). A desktop-name-only check reads "unsupported"
  on WebGL even though DXT1 works. Tokens are identical (`GL_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1`).
- Block-aligned DXT1 uploads compressed to hardware. Non-4-block-aligned DXT1 (e.g. a 128×2 gradient) is
  **rejected** by WebGL2/ANGLE (`glCompressedTexImage2D: Invalid compressed image size`) and is
  software-decoded to RGBA8 via GL4ES's decoder, vendored at `Src/decompress.cpp` + `Inc/decompress.h`.

## 7. Native oracle (sanity twin)

`cmake --preset native && cmake --build --preset native` builds the 32-bit SDL2/GLES2 native client
(XOpenGLDrv + NOpenGLESDrv both). It's the compat oracle — if the WASM build misbehaves, confirm the same
scene on native first (note: the on-screen window is black on this box's Xwayland for *all* drivers — verify
via FBO readback or the web probe, not the native window).
