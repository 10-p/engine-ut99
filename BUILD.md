# Building engine-ut99 (WebAssembly + native)

The **private engine repo** for the **UnrealTournament (1999), Unreal Engine 1 v400** web port — engine
source only. Game assets (retail UT99 v400: maps/textures/music/sounds + the `.u`/`.int` packages) live
**out of tree** and are never tracked here; no build reads them. Base
import: [maximqaxd/ut99dc](https://github.com/maximqaxd/ut99dc) (kept as the `upstream` remote).

The public monorepo `ufront` ([10-p/unreal-frontend-arch-private](https://github.com/10-p/unreal-frontend-arch-private)) consumes this repo's WASM artifact, owns the host page
/ UI / glue / assembly, and injects the heavy glue behind the thin ABI shim. **Migration state lives in the
monorepo** (`docs/migration/STATE.md`), not here.

## Build matrix (CMake presets — `Source/CMakePresets.json`, run from `Source/`)

| Preset | Output | Purpose |
|--------|--------|---------|
| `native` | `build-native/UnrealTournament/UnrealTournament` + `libopenal.so.1` | 32-bit native SDL2/GLES2 twin (the **compat oracle**; needs a 32-bit SDL2, see README) |
| `wasm` | `build-wasm/SDLLaunch/SDLLaunch.{js,wasm,data}` + `meta.json` | Emscripten **dev** (assertions + debug) |
| `wasm-perf` | `build-wasm-perf/SDLLaunch/…` | Emscripten **product** (`-O3`) — what `ufront` consumes |
| `windows` | `build-windows/UnrealTournament/UnrealTournament.exe` + `OpenAL32.dll` | 32-bit **Windows** binary via MinGW-w64 — *step 2.33, see below* |

```bash
cd Source
cmake --preset native && cmake --build --preset native -j"$(nproc)"        # the native twin / oracle
cmake --preset windows   && cmake --build --preset windows   -j"$(nproc)"        # the Windows exe
source <emsdk>/emsdk_env.sh   # emsdk 5.0.7; sets $EMSDK for the toolchain file
cmake --preset wasm-perf && cmake --build --preset wasm-perf -j"$(nproc)"        # the web build
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
otherwise). Backends are whatever this host's headers allowed at configure time — e.g. PulseAudio, ALSA,
OSS, each dlopen'd at run time — and the configure log says which
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

## 64-bit (LP64) — ufront 2.83

UT99 runs as a 64-bit engine on Linux x86_64 and Android arm64 (and still on every 32-bit target), with the
**retail 32-bit `.u` packages unchanged**. The plan, the evidence and the Unreal 1 playbook are in the monorepo:
`docs/migration/steps/2.83-64bit-ue1.md`; the portable lessons in `docs/10` §10.

| Preset | Output | Notes |
|--------|--------|-------|
| `native64` | `build-native64/` — `UnrealTournament`, `UCC/ucc-bin` (ELF 64-bit) | `UT_ALLOW_64BIT=ON`; needs the amd64 `libsdl2-dev` (see below) |
| `native64-asan` | `build-native64-asan/` | AddressSanitizer; run with `ASAN_OPTIONS=alloc_dealloc_mismatch=0:verify_asan_link_order=0` (the second because `stdbuf` preloads a library) |
| `android-arm64-ucc` | `build-android-arm64/UCC/ucc-bin` (aarch64, bionic) | headless server + commandlets only; `ANDROID_NDK_HOME=<android-sdk>/ndk/30.0.14904198` |
| `android-armv7-ucc` | `build-android-armv7/UCC/ucc-bin` | the 32-bit control on the same ARM device |

**What makes it work** (each is a commit on `main`; the mechanisms are general, not `#if`-guarded):

- **Script object references are 4-byte object indices** in memory (`UObject::GetScriptObject`), so the bytecode
  keeps the layout the 32-bit compiler gave it: no jump, label, replication or saved-state offset moves.
  The readers are `ALWAYS_INLINE` (the VM file is compiled at `-Og`): the index costs nothing measurable
  (`U64Bench`, 9M iterations, i386: 0.68 s vs 0.69 s before 2.83).
- **Every bytecode operand is read by copy** (`ReadScriptOperand<T>`): the stream is packed bytes, and 32-bit ARM
  faults on an unaligned multi-word load (`execVectorConst`, SIGBUS).
- **The mirror contract:** every header declaring a scripted native class or a script-mirrored struct is
  `#pragma pack(push,4)`; object properties align to 4; `NativePlaceholderSize` (UnClass.cpp) gives the script
  `int` stand-ins for C++ pointers (`ObjectInternal`, secondary vtables, private pointers, UWeb's TMaps) their
  C++ size. A C++/script size mismatch is now **fatal** (`appErrorf`), not a warning.
- **A None context clears its whole destination** (`execLet`): the compiler recorded 4 bytes for an object, and
  `X = None.Y` left the high half of X's old pointer behind.
- **The compiler writes the format's 32-bit value sizes on every host** (`ScriptFormatStructSize`): a 64-bit
  `ucc make` produces a package byte-identical to the 32-bit one but for the GUID.
- Registration stashes live in pointer-sized fields; `PTRINT`/`UPTRINT` wherever a pointer passes through an
  integer; a POSIX build without SDL keeps time with `CLOCK_MONOTONIC`; ARM gets `-fsigned-char`.

**Host prerequisite.** The 64-bit SDL2 development files beside the 32-bit ones (Debian: `libsdl2-dev:amd64`,
whose missing `/usr/include/x86_64-linux-gnu/SDL2/_real_SDL_config.h` is how its absence shows up; Arch:
`sdl2-compat`). `ucc-bin` needs neither.

**Test loops** (all headless):

```bash
tools/dev/mkrun-ut99.sh build-native64 <run-tree>      # + copy UCC/ucc-bin and libopenal.so.1
cd <run-tree>/System
gdb -batch -x <repo>/tools/dev/layout-sweep.gdb --args ./ucc-bin make          # classes: shifted=1 (EditorEngine.Pad); structs: 0
DISPLAY=:98 gdb -batch -x <repo>/tools/dev/layout-sweep.gdb --args ./UnrealTournament -SOFTWARERENDERER   # client host: 0 / 0
./ucc-bin server "DM-Deck16][?game=Botpack.DeathMatchPlus?mutator=U64Test.U64Mutator"   # U64 DONE passed=63 failed=0
<repo>/tools/dev/botmatch.sh <server-tree> 300 [<client-tree>] [<map>]        # bots need MinPlayers in the ini
```

**Sweep both hosts.** Some reflected layouts exist in only one process: the input alias struct (`UInput::StaticInitInput`)
is built only where there is a client or the editor, so `ucc make` never saw its 64-bit layout, and every key
bound to an alias crashed the 64-bit client (`65e2f5a`). The client host stops at the first game tick.

`tools/test/U64Test` is compiled by the **32-bit** `ucc make` (link `../U64Test` into the run tree, add
`EditPackages=U64Test`) and run on every width. On the phone: push the asset tree to `/data/local/tmp/ut99/`
(`adb push` per folder, ~16 s for 850 MB), push `ucc-bin`, run it from `adb shell`; a desktop client joins over
Wi-Fi by the phone's address (UDP cannot be `adb forward`ed).

**Coverage census — what has never run at 64-bit** (`tools/dev/census/`). The gates above are bots, a
headless client and a compile; three defects found after 2.83 closed were all on paths only a player
reaches (the alias key, the SoftDrv lit palette, `shot`). A `--coverage` build, run through everything, says
which functions never executed; read those, or drive them.

```bash
cc -c -O2 -fPIC tools/dev/census/gcovdump.c -o /tmp/gcovdump.o     # writes counters on SIGTERM and on a crash
cd Source && cmake --preset native64 -B ../build-native64-cov -DCMAKE_C_FLAGS=--coverage -DCMAKE_CXX_FLAGS=--coverage \
    "-DCMAKE_EXE_LINKER_FLAGS=--coverage /tmp/gcovdump.o" -DCMAKE_SHARED_LINKER_FLAGS=--coverage && cmake --build ../build-native64-cov
tools/dev/mkrun-ut99.sh build-native64-cov <tree>                    # + ucc-bin, libopenal.so.1; the objects' .gcda merge across runs
# run the gates against <tree>, then player sessions with every render device (Xvfb :98, loopback only):
unshare -rn tools/dev/census/play.sh <tree> -XOPENGLRENDERER "DM-Morpheus?game=Botpack.DeathMatchPlus" /tmp/p shot "stat global" "rmode 1"
python3 tools/dev/census/census.py build-native64-cov Source build-native64-cov/_deps/{utglr,xopengldrv}-src   # -> census.json
tools/dev/census/exports.sh <32-bit tree> /tmp/e32; tools/dev/census/exports.sh <64-bit tree> /tmp/e64; diff -r /tmp/e32 /tmp/e64
```

2.83: 3,578 of 5,008 functions ran at x86_64 (gates alone: 3,272). The 1,430 that never ran were read for LP64
defects (none open: editor GUI, the 15/16-bit and MMX SoftDrv variants, Windows-only code, the unused SoundDrv).
Traps: a run tree copied from the reference one puts XOpenGL in its `ES_WEBGL` profile (delete `OpenGLVersion` to
reach the desktop Core paths); the renderers, UWeb, TcpLink, ice/scripted textures and downloads only run when a
session asks for them (web admin `[UWeb.WebServer] bEnabled=True`, a client without a `ServerPackages` entry).

**Fixed on the way (not 64-bit): a dedicated server died at the 2nd or 3rd map change** (`Bad name index`,
`fread failed`). `appCreateProc` had its fork branches inverted (as in Epic's v400 Linux code), so the stats
log's batcher launch at every match end left a full copy of the server running on the same open package files.
`6a74d44`; `botmatch.sh <tree> 300 "" "DM-Deck16][?FragLimit=2"` now plays five or six maps.
Two more, found by the census and the ASan gate (Epic's code, every width including the web): a client's
travel read a freed net channel in `UObject::ConditionalDestroy` (`3d78837`; `botmatch.sh` now counts sanitizer
reports, `d4e148e` — an ASan abort in the client used to look like a player leaving), and `shot` (F9) read a
row past its buffer on every non-MSVC build (`9d74a86`).

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
cmake --preset native -DFETCHCONTENT_SOURCE_DIR_XOPENGLDRV=<path>/XOpenGLDrv
cmake --preset native -DFETCHCONTENT_SOURCE_DIR_UTGLR=<path>/utglr-copy
```

The first configure of a fresh build dir needs network (the fetch); subsequent configures reuse `_deps/`.

## Windows builds (MinGW-w64) — step 2.33

```bash
cd Source
cmake --preset windows && cmake --build --preset windows -j"$(nproc)"   # -> build-windows/UnrealTournament/UnrealTournament.exe
```

One 32-bit `.exe` (plus `OpenAL32.dll` beside it): `BUILD_STATIC` links every package in, so `appGetDllHandle` resolves
through the internal registry and there is **no `Core.dll`/`Engine.dll` and no `LoadLibrary`** — the same
mechanism the web and Dreamcast builds use. `objdump -p` shows only stock system DLLs and `OpenAL32.dll`; libgcc, libstdc++ and winpthread are linked
statically (a posix-thread-model MinGW such as MSYS2's or Arch's otherwise adds `libwinpthread-1.dll`).

⚠ **32-bit only on Windows.** LLP64 (Windows x64) has never been built, so `Source/CMakeLists.txt` refuses a 64-bit
MSVC and the toolchain file targets `i686-w64-mingw32` (64-bit Linux and Android are the `native64` and
`android-*` presets, ufront 2.83); it supplies only what a *cross* build cannot infer,
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
export WINEDLLOVERRIDES="mscoree,mshtml="   # a new prefix otherwise waits on the Mono/Gecko installer dialogs
DISPLAY=:99 WINEPREFIX=<prefix> wine UnrealTournament.exe -SOFTWARERENDERER   # or -NULLRENDERER
WINEPREFIX=<prefix> wineserver -k          # ⚠ always; wine leaves processes behind
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

At step 2.7a the `native` preset was the minimal client; it now links the full static set (Editor, every
renderer, OpenAL — see `Source/CMakePresets.json`).

## Shared with the other engines of the frontend (2026-10-11, from engine-uc2)

The same commands, settings and log lines in every engine (ufront's UC2 merge; engine-uc2's
`docs/CHEAT-COMMANDS.md` is the reference for the cheats):

- **`WHEREAMI`** prints `<x> <y> <z> <yaw> <pitch>` (the pawn's location, one decimal; the view's yaw 0..65535 and
  pitch -32768..32767) on the console, on screen and in the log; **`TELEPORT <x> <y> <z> [<yaw> [<pitch>]]`** puts the
  local player there (WHEREAMI's output pasted back gives the same place and view). Cheats: allowed in a standalone
  game and for a listen server's host, refused (logged) on a network client; `[Engine.GameEngine] AllowNetCheats=True`
  opens them for test harnesses only (`-ini:`), never in a shipped ini. Every use logs a `Cheat: ` line.
  `Source/Engine/Src/UnCheats.cpp`, first in `UGameEngine::Exec` (the web's `ue_exec_command` reaches the engine
  only, and the typed console ends there too).
- **`[Engine.GameEngine] FpsLogSeconds=<s>`** (0 = off) / console **`FPSLOG <s>`**: an `FPS:` log line every `<s>`
  seconds (frames, fps, frame ms avg/min/max/p99, the 1% low, the viewport size). `Source/Engine/Src/UnFpsLog.cpp`.
- **`Init: Language: <ext>`** in the log at start-up: the language this run took (`[Engine.Engine] Language`, read
  after the `-ini:` overrides; the frontend's Language setting).
- **The command line holds 4,096 characters** (launcher, UCC, Core's `GCmdLine`): a token that does not fit is
  dropped whole, with a warning once the log is up. It was a `TCHAR[1024]` filled with `strcat`, which the web's
  argv (the URL plus every `-ini:` seed) could overrun.
- **`FindFiles` matches like Windows' `FindFirstFile`** (`Core/Inc/FFileManagerLinux.h`, engine-hp2's fix of the
  same code): `*`/`?` anywhere, case-insensitive (`*.INT` finds `Core.int`), `*.*` = everything, and the
  Files/Directories flags honoured (no `.`/`..`). It was case-sensitive and took `*.ext` as a substring test (`*.u`
  also returned `.uax/.umx/.utx/.unr`), and a Tree `DeleteDirectory` failed on the `.` it got back.
- **`ucc make` saves are deterministic** (`Core/Src/UnObj.cpp`, engine-uc2's S2): `SavePackage` sorts names,
  imports and exports with a stable merge sort (the C library's qsort left equal keys in a library-dependent
  order), and `CacheDrivers` reads the `.int` files in Windows' (NTFS) order, not `readdir()`'s.
- **Game pads** (`Source/NSDLDrv`, one pad: the first connected): `[NSDLDrv.NSDLClient] UseJoystick` decides
  whether pads are used at all (it was registered and read by nothing); a pad connected after start opens on
  `SDL_CONTROLLERDEVICEADDED` and closes on `REMOVED`, releasing every pad key (there was no hot-plug, so on the web,
  where a browser lists a pad only at its first press, a pad practically never worked); and on the web the press
  that made the browser list the pad, which SDL's Emscripten driver never reports, is read from
  `navigator.getGamepads()` once at the connect and given to the game (engine-uc2's H-3). Log lines: `NSDLClient:
  game pad connected: <name>` / `disconnected` / `buttons held at the connect given to the game: 0x<mask>`.
  That press is a tap (released at the next tick: SDL never saw it go down, so it never reports its release). The
  sticks and triggers are read from the pad's STATE every tick, not from axis events: SDL's Emscripten driver reports
  no axis values when a pad is opened, so SDL took a stick's first push for its resting point and posted no event for
  it (`SDL_PrivateJoystickAxis`); the state is right (engine-uc2 reads pads the same way). Found by the cross-engine
  E2E (`wasm-shared.spec.js` connect-and-move), with: a stick's step is scaled by the frame time, which was a `FLOAT`
  copy of `appSeconds()`. On the threaded web build that is an epoch-sized number (`emscripten_get_now()` is
  `timeOrigin + now` there, so that every worker agrees), which a `FLOAT` holds in 128-second steps: the time step was
  0 on nearly every frame and a held stick moved nothing. `TickInput` keeps the time in a `DOUBLE` now.

## The web port (WASM)

Mirrors engine-unreal1's web layer (the portable UE1→web lessons, monorepo `docs/10`):
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
