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
| `windows` | `build-windows/UnrealTournament/UnrealTournament.exe` | 32-bit **Windows** binary via MinGW-w64 — *step 2.33, see below* |

```bash
cd Source
cmake --preset native && cmake --build --preset native -j"$(nproc)"        # the native twin / oracle
# (2.7b+) source ~/emsdk/emsdk_env.sh   # sets $EMSDK for the toolchain file
# (2.7b+) cmake --preset wasm-perf && cmake --build --preset wasm-perf -j"$(nproc)"
# (2.33)  cmake --preset windows   && cmake --build --preset windows   -j"$(nproc)"
```

### `meta.json` records what was fetched (ufront 2.70)

`toolchain` names every repository FetchContent pulled, as `<repo>@<branch|tag>` — never a commit: the
project-owned repositories (`10-p/*`) are consumed at their branch tip so a rebuild takes the latest of
each, and an external upstream (`libxmp/libxmp@libxmp-4.7.1`) stays on its release tag. The values come from
the same cache variables the `FetchContent_Declare` read (`Source/cmake/ArtifactDeps.cmake`), so the
descriptor cannot drift from the fetch; a local checkout override (`FETCHCONTENT_SOURCE_DIR_<NAME>`,
`LIBXMP_SOURCE_DIR`, `GL4ES_SOURCE_DIR`) shows up as `local:<path>`. ⚠ A branch ref is re-fetched on every
**configure** (`cmake --preset …`), never by a plain `cmake --build`: an old build directory rebuilt without
re-configuring still holds the dependency it fetched last time.

### openal-soft on native Linux (ufront 2.70)

The native binary no longer links the distro's `libopenal`: `Source/cmake/OpenALSoft.cmake` (the 2.61 web
module, generalised — `ufront_openal_soft(PLATFORM WEB|NATIVE …)`) FetchContent's the same fork the web uses
(`10-p/openal-soft-web` `ufront/web`) and builds it SHARED for the `native` preset. The launcher links it
(`DT_NEEDED libopenal.so.1`), carries `$ORIGIN` in its rpath and copies `libopenal.so.1` beside itself, so
**a run tree that takes the binary must take `libopenal.so.1` with it** (the loader stops before `main`
otherwise). Backends are whatever this host's headers allowed at configure time — PulseAudio, ALSA, OSS on
this box, each dlopen'd at run time — and the configure log says which
(`Building OpenAL with support for the following backends`). `-DUT_BUNDLED_OPENAL=OFF` restores the system
library. Why: the system library is whatever the distro built, so the native binary carries the fork to play
what the web plays.

### Running the native oracle
The client runs from a `System/` dir (cwd), reading assets via relative `../Maps`, `../Textures`, … (the
`[Core.System] Paths`). A run tree is assembled by symlinking `$UT99_ASSETS_DIR/{Maps,Textures,Sounds,
Music}` at the game root and the asset `System/*.{u,int}` + the built `*.so` + client into `System/`, with
a `UnrealTournament.ini` selecting `NOpenGLESDrv.NOpenGLESRenderDevice` + `NSDLDrv.NSDLClient` +
`SoundDrv.NullAudioSubsystem`. It needs a real display + GL context (or `xvfb` for headless). Verified
(step 2.7a): loads the v400 engine + all v68 retail packages + a real DM map — proving v400↔v68 asset
compatibility.

**`ucc-bin` (target `UCC`, `build-native/UCC/ucc-bin`)** is self-contained on a static preset (ufront 2.79):
it whole-archives the headless package set (Core, Engine, Render, IpDrv, UWeb, Fire, + Editor), so
`./ucc-bin server "DM-Deck16][?game=Botpack.DeathMatchPlus" -nohomedir` runs a dedicated server from
`System/` with no package `.so` beside it (no `libopenal.so.1` either: it links no audio or render device).
v400 has no `compress` commandlet (retail v400 had none either).

## Render drivers — fetched, not vendored

The two shared HW render drivers are **pulled from their own GitHub repos at configure time** (CMake
`FetchContent`), not vendored in this tree — only each driver's build-glue `CMakeLists.txt` lives here
(`Source/XOpenGLDrv`, `Source/OpenGLDrv`):

| Driver | Package | Repo (`10-p` fork) | Default ref |
|--------|---------|--------------------|-------------|
| **XOpenGLDrv** (OldUnreal GL → WebGL2/GLES3) | `XOpenGLDrv.XOpenGLRenderDevice` | `github.com/10-p/XOpenGLDrv` | branch `ut99-v400-compat` |
| **OpenGLDrv / UTGLR** (Dohnal legacy GL; web via gl4es) | `OpenGLDrv.OpenGLRenderDevice` | `github.com/10-p/utglr-copy` | branch `main` |

(`gl4es` and `libxmp` are likewise fetched — see the top-level `Source/CMakeLists.txt` + `NOpenALDrv/`.)
`SoftDrv`, `NullDrv`, `NOpenGLESDrv` remain in-tree (engine-specific). **Editing a driver** means editing
its repo, then bumping the pinned ref here (`XOPENGL_GIT_TAG` / `UTGLR_GIT_TAG`; pin to a commit for full
reproducibility). To build against a **local checkout** without pushing (edit → build → push → bump ref):

```bash
cmake --preset native -DFETCHCONTENT_SOURCE_DIR_XOPENGLDRV=/home/ion/engines/XOpenGLDrv
cmake --preset native -DFETCHCONTENT_SOURCE_DIR_UTGLR=/home/ion/engines/utglr-copy
```

The first configure of a fresh build dir needs network (the fetch); subsequent configures reuse `_deps/`.

## Windows builds (MinGW-w64) — step 2.33

```bash
cd Source
cmake --preset windows && cmake --build --preset windows -j"$(nproc)"   # -> build-windows/UnrealTournament/UnrealTournament.exe
```

One self-contained 32-bit `.exe`: `BUILD_STATIC` links every package in, so `appGetDllHandle` resolves
through the internal registry and there is **no `Core.dll`/`Engine.dll` and no `LoadLibrary`** — the same
mechanism the web and Dreamcast builds use. `objdump -p` shows only stock system DLLs.

⚠ **32-bit only.** `Source/CMakeLists.txt` demotes an x86_64 compiler to `-m32` and `FATAL_ERROR`s if it
cannot. The toolchain file targets `i686-w64-mingw32`; it supplies only what a *cross* build cannot infer,
and is the same MinGW-w64 GCC that MSYS2's `MINGW32` environment ships, so the preset also works from an
MSYS2 prompt (where pacman's SDL2 answers instead of the fetched one).

**The theme of the port: the compiler is not the platform.** Almost every failure was a `#if _MSC_VER` or
`#if __GNUG__` used to mean "Windows" or "POSIX" — true while GCC only ever meant Unix, false the moment
GCC targeted Windows. Rekeyed onto `PLATFORM_WIN32` (or IpDrv's own `__WINSOCK__`/`__BSD_SOCKETS__`):

- **The platform layer itself** (`Core/CMakeLists.txt`) — `UnVcWin32.cpp` is `#if _MSC_VER` throughout, so
  under MinGW it compiled to an *empty* file while the same list dropped `UnUnix.cpp` for being a Windows
  target: a link missing ~20 `app*` symbols with nothing to explain it. MSVC now gets the Win32 core;
  every GCC/Clang target keeps the portable `UnUnix.cpp` that web and Linux already exercise, with the
  handful of genuinely Win32 calls in **`Core/Src/UnMinGW.cpp`** (`appGetSystemErrorMessage`, `appGetGUID`
  via `CoCreateGuid`, `WSAStartup`) — a file Linux and web never compile, which is where Windows work
  belongs by default.
- **IpDrv** — WinSock vs BSD sockets, the resolver thread (`CreateThread` vs `pthread_create`), `fcntl`
  vs `ioctlsocket`, `socklen_t` (one `<ws2tcpip.h>` include beats an `#if` at every call site).
- **`Engine/Src/UnTex.cpp`** — the DXT block was `#ifdef WIN32` but calls Epic's `s3tc.lib`, which **no
  CMake target links**; it only ever built under Epic's original MSVC project, so `WIN32` meant `_MSC_VER`.
- **`Editor/Src/UnParams.cpp`** — its duplicate-symbol guard said `__EMSCRIPTEN__`, but the cause is
  whole-archiving, i.e. `UNREAL_STATIC`. The web was merely the first static build we shipped.

Two ABI differences, both real:

- **`-fpack-struct=4`.** UE1 assumes Win32 `/Zp4`: every reflected C++ class size must equal its
  UnrealScript property size. Native x86-32 Linux matches by luck (its ABI aligns `double` to 4); MinGW
  follows the MSVC ABI and aligns to 8, so every class holding a double came out long. ★ **2.32's layout
  guard named every offending class on the first boot** — the silent version of this is what 2.32 existed
  to hunt down.
- **`ABrush::bColoredPad` is NOT applied on MinGW.** That pad compensates for GCC packing a lone trailing
  bitfield into one byte; MinGW-w64 lays bitfields out the *MSVC* way (`-mms-bitfields` is its default),
  so the DWORD is already there and the pad overshot by 4. `DWORD`/`SIZE_T`/`BITFIELD` likewise come from
  `<minwindef.h>` on Windows and must stay *identical* types, not merely same-width — UE1 serialises
  BITFIELD members through `operator<<`'s DWORD overload.
- **A size check is not an offset check (ufront 2.80).** GCC and clang (Itanium layout) disagree with MSVC
  — and so with UE1's `UStruct::Link` — in three more ways: a derived class's first member reuses its
  base's **tail padding** (`APlayerStart::TeamNumber`), a `BYTE` after bitfields is packed into the
  bitfield unit's spare bytes (`UEditorEngine::AutosaveTimeMinutes`), and a bitfield after `BYTE`s is
  packed into their unit (`UIceTexture::MoveIce`); a four-`BYTE` `FColor` is also 1-aligned where retail's
  is a DWORD union (`AZoneInfo::FogColor`). Each is fixed in the header with `GCC_ALIGN(4)`/`GCC_PACK(4)`
  (empty on MSVC; a no-op under MinGW's MS bitfields). The runtime guard missed some of them — a later
  member can re-align the size — so check OFFSETS with **`tools/dev/layout-sweep.gdb`** (every script
  property of every native class vs the same-named C++ member, run under `ucc-bin make`; healthy = only
  the placeholder `EditorEngine.Pad` differs, 1,432 properties at 2.80).

**Verifying without Windows** — `wine` + `Xvfb` (needs `wine32`; SoftDrv/NullDrv are CPU-only, so no GPU):

```bash
Xvfb :99 -screen 0 1024x768x24 &
DISPLAY=:99 WINEPREFIX=~/.wine-u1 wine UnrealTournament.exe -SOFTWARERENDERER   # or -NULLRENDERER
WINEPREFIX=~/.wine-u1 wineserver -k          # ⚠ always; wine leaves processes behind
```

Assert on **milestones in `System/UnrealTournament.log`**, not on a window appearing. Run-tree traps: copy
the `.exe` in (the engine derives its base dir from the real binary path) and dereference asset symlinks
(`cp -rL`), or every package becomes a silent `FileNotFound`. ⚠ wine is not Windows — a regression net,
not a substitute for a real-hardware pass.

Verified at 2.33: boots to `CityIntro` on SoftDrv with **zero layout warnings**, and step 2.32's
multi-keyframe mover fixture run against the `.exe` reproduces the web oracle — red 100 % through t=5 s,
flipping green at t=6 s and stable at 53.6 % (web records 53.2 %), i.e. movers run their whole sequence.

**Out of scope for now:** hardware renderers on Windows (XOpenGL/UTGLR/NOpenGLES), and `ucc.exe`.

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
