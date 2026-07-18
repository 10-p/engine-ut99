# Building engine-ut99 (WebAssembly + native)

The **private engine repo** for the **UnrealTournament (1999), Unreal Engine 1 v400** web port — engine
source only. Game assets (retail UT99 v400: maps/textures/music/sounds + the `.u`/`.int` packages) live
**out of tree** at `$UT99_ASSETS_DIR` (default `/home/ion/ut99-assets`) and are never tracked here. Base
import: [maximqaxd/ut99dc](https://github.com/maximqaxd/ut99dc) (kept as the `upstream` remote).

The public monorepo `ufront` (`/home/ion/ufrontend`) consumes this repo's WASM artifact, owns the host page
/ UI / glue / assembly, and injects the heavy glue behind the thin ABI shim. **Migration state lives in the
monorepo** (`docs/migration/STATE.md`), not here.

## Build matrix (CMake presets — `Source/CMakePresets.json`, run from `Source/`)

| Preset | Output | Purpose |
|--------|--------|---------|
| `native` | `build-native/UnrealTournament/UnrealTournament` + `*.so` | 32-bit native SDL2/GLES2 twin (the **compat oracle**; needs `libsdl2-dev:i386`) |
| `wasm` | `build-wasm/…/Launch.{js,wasm,data}` + `meta.json` | Emscripten **dev** (assertions + debug) — *wired in step 2.7b* |
| `wasm-perf` | `build-wasm-perf/…` | Emscripten **product** (`-O3`) — what `ufront` consumes — *wired in step 2.7b* |

```bash
cd Source
cmake --preset native && cmake --build --preset native -j"$(nproc)"        # the native twin / oracle
# (2.7b+) source ~/emsdk/emsdk_env.sh   # sets $EMSDK for the toolchain file
# (2.7b+) cmake --preset wasm-perf && cmake --build --preset wasm-perf -j"$(nproc)"
```

### Running the native oracle
The client runs from a `System/` dir (cwd), reading assets via relative `../Maps`, `../Textures`, … (the
`[Core.System] Paths`). A run tree is assembled by symlinking `$UT99_ASSETS_DIR/{Maps,Textures,Sounds,
Music}` at the game root and the asset `System/*.{u,int}` + the built `*.so` + client into `System/`, with
a `UnrealTournament.ini` selecting `NOpenGLESDrv.NOpenGLESRenderDevice` + `NSDLDrv.NSDLClient` +
`SoundDrv.NullAudioSubsystem`. It needs a real display + GL context (or `xvfb` for headless). Verified
(step 2.7a): loads the v400 engine + all v68 retail packages + a real DM map — proving v400↔v68 asset
compatibility.

## What the native port took (step 2.7a)

ut99dc already builds native/desktop; two gaps had to be filled for a fresh gcc-15 checkout:
- **`Source/SoundDrv/` (null audio)** — the shipped inis select `SoundDrv.NullAudioSubsystem`, but ut99dc
  never included the package (only the Dreamcast `AICADrv` and an OSS-`/dev/dsp` `Audio` subsystem, neither
  usable on native-Linux/WASM). `SoundDrv/Null.cpp` is a silent `UAudioSubsystem` (v400 interface), mirroring
  engine-unreal1's `SoundDrv`.
- **`Thirdparty/glad_es` + `Thirdparty/glm`** — `NOpenGLESDrv` needs a glad GLES2 loader + glm; ut99dc
  doesn't commit them. Vendored glad 0.1.36 (gles2, with `APPLE/EXT_texture_format_BGRA8888` + `MESA_bgra` +
  `EXT_depth_clamp` — exactly the extensions `NOpenGLESDrv.cpp` references) + glm, from engine-unreal1.

The `native` preset builds the minimal client (`BUILD_EDITOR=OFF`, `BUILD_DCUTIL=OFF`): Core, Engine,
Render, IpDrv, Fire, NSDLDrv, NOpenGLESDrv, SoundDrv, UnrealTournament.

## The web port (WASM) — step 2.7b+

Mirrors engine-unreal1's web layer (the portable UE1→web lessons, monorepo `docs/10`). To be filled in as
each slice lands:
- **`if(EMSCRIPTEN)` CMake branch** — `BUILD_STATIC`, `ASM=0`, exceptions-off, `-fpack-struct=4`, the
  web-only `WebRTCDrv`, GLES2→WebGL, POSIX/socket guards. *(2.7b)*
- **Main-loop inversion** (`Source/UnrealTournament/Src/Launch.cpp`) — the blocking `MainLoop` yields each
  frame via `emscripten_sleep` (Asyncify). *(2.7b)*
- **`appBaseDir()` = `getcwd`** on Emscripten; only `System/Default.ini` preloaded, everything else streams
  via the monorepo's FetchFS. *(2.7b/2.7c)*
- **Exceptions off → load failures resolve to NULL**, patched one site at a time (the `[appThrowf]` console
  line names each); the Editor package linked (bot meshes/skins); GLog→console. *(2.7c/first-light)*
- **`WebRTCDrv`** — a UE1 `UNetDriver`/`UNetConnection` over the glue `webrtc_*` ABI; auto-switched from
  `IpDrv.TcpNetDriver`; `ue_exec_command` (ADR-10). *(2.7d)*
