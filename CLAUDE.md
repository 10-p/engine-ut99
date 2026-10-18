# engine-ut99 — agent guide

**What this is:** the private engine repo for **Unreal Tournament (1999), Unreal Engine 1 v400**: engine source
only. This branch, `feat/v436-compat` (cloned as `engine-ut99-436-compat`), runs the retail **v436** packages, maps
and v436-era mods (`ENGINE_VERSION` 436, `PACKAGE_FILE_VERSION` 69). It builds a WebAssembly artifact, the frontend's
`ut99-436-compat` target (and native Linux, Android and Windows binaries). The frontend repo
`10-p/unreal-frontend-arch-private` consumes the artifact and owns the host page, the UI, the build assembly and the
browser glue behind a thin ABI. Building, options and test tools: `BUILD.md`.

**Lineage:** Epic's v400 drop, ported by [maximqaxd/ut99dc](https://github.com/maximqaxd/ut99dc) up to `820b25c`; our
work is everything after that commit. The baseline is the Epic drop in `~/WorkUnreal/epic-sources/` (`ut99-v400/`,
see its README). No `upstream` remote is configured: compare against `820b25c` or the drop.

**Features** are listed in the frontend's `docs/14-change-inventory.md`; a new feature adds its line there.

**`feat/v436-compat` holds v436-specific changes only.** A general engine change goes to base `engine-ut99` `main`,
and this branch is then rebased on top (`git fetch origin && git rebase --onto origin/main <old-base>`; a rebase is
agreed before it is made, since it rewrites a pushed branch). A general change committed here directly is a parallel
copy that the next fix to one side misses. General and v436-specific work stay in separate commits so a rebase stays
cheap. The frontend's `publish-all` refuses this branch when it does not contain `origin/main`.

## Map
| Thing | Where | Role |
|-------|------|------|
| This repo (private) | `10-p/engine-ut99` | UT99 v400 source → web and native builds |
| The v436 branch | `feat/v436-compat` | the same engine for v436 content; carries all of `main` |
| Frontend | `10-p/unreal-frontend-arch-private` | consumes `build-wasm-perf/SDLLaunch/`; host, UI, glue, publishing |
| Fetched renderers | `10-p/XOpenGLDrv` (`ufront`), `10-p/utglr-copy` (`main`) | edited in their repos; `Source/XOpenGLDrv` and `Source/OpenGLDrv` hold the build glue |
| Game assets | any local folder (never in git) | a retail UT99 install; no build reads it |
| Android app | `Android/` | the Gradle/NDK project: `libUT99.so` from `Source/CMakeLists.txt`, the launcher and the touch overlay (`BUILD.md`, Android) |
| Android reference | [Andiweli/UT99-Android](https://github.com/Andiweli/UT99-Android) | SDL2 glue ideas only |

## Load-bearing facts
- **One static binary on every preset** (`BUILD_STATIC`, `UNREAL_STATIC`, `--whole-archive`); packages resolve
  through `Core/Src/UnStaticExports.cpp`. The Editor package is linked everywhere: maps and meshes reference it.
- **`ASM=0` off MSVC:** `Core/Inc/UnGnuG.h` supplies the portable maths (`appRound` ties to even, as `fistp` does);
  SoftDrv takes its portable C paths on every build.
- **Struct layout:** UE1 assumes Win32 `/Zp4`. The web and MinGW builds pass `-fpack-struct=4` (ours; x86-32 Linux
  matches without it); every header declaring a scripted native class or a script-mirrored struct is
  `#pragma pack(push,4)`, which also holds the layout at 64-bit. A C++/script size mismatch is fatal at load;
  `tools/dev/layout-sweep.gdb` checks offsets.
- **64-bit** runs the retail 32-bit `.u` unchanged: script object references are 4-byte object indices
  (`BUILD.md`, 64-bit).
- **The compiler is not the platform:** Windows code keys on `PLATFORM_WIN32` (IpDrv on `__WINSOCK__`/
  `__BSD_SOCKETS__`); MSVC builds `Core/Src/UnVcWin32.cpp`, every GCC/Clang target `Core/Src/UnUnix.cpp`, with
  MinGW's Win32 calls in `Core/Src/UnMinGW.cpp`.
- **Main loop on the web:** `MainLoop` returns to the worker's event loop every frame through Asyncify (the
  `ufront_wait_frame` `EM_ASYNC_JS` wait: the display's next frame, a timer while the page is hidden or with
  `-TIMERFRAMES`), never `emscripten_set_main_loop`, whose unwind collides with `main()`'s try/catch.
- **Files:** the engine uses libc `fopen`/`opendir` (`FFileManagerLinux`); on the web WasmFS's JSImpl backend
  (`vendor/glue/wasmfs-shim.js`) serves them from the host. Do not write an engine-side web file manager.
  `appBaseDir()` is the working directory (the launcher `chdir`s to `System`).
- **Glue ABI:** `vendor/glue/` (`ue_abi.h`, `net-shim.js`, `wasmfs-shim.js`) is vendored from the frontend at ABI
  version 2. Only these thin forwarders and `Source/UnrealTournament/msaa-backbuffer.js` are baked into the wasm;
  the transport, the file system and storage are injected by the host at run time. A change to the ABI is a
  cross-engine change owned by the frontend: re-vendor, never edit here.
- **The log reaches stdout on every build** (`Core/Inc/FOutputDeviceFile.h`, the same lines as the log file); the
  frontend's E2E reads engine milestones from the browser console. Do not remove it, and do not reword a log line
  the frontend matches (grep `packages/e2e` and `targets/` in the frontend first).
- **Exceptions on the web:** engine objects cannot catch, so web loading fails soft (`GetPackageLinker`,
  `StaticLoadObject`, `SafeLoadError`, `LoadMap` resolve to NULL or a failed join; a bad startup map falls back to
  the menu) under `#ifdef __EMSCRIPTEN__`, and `appThrowf` prints `[appThrowf] <message>` on every build. Native
  keeps Epic's errors. The link enables catching only for openal-soft's `-fexceptions` objects; leave the engine's
  objects as they are.
- **Net on the web:** WebRTCDrv (`UWebRTCNetDriver`) over the `ue_net_*` imports, with a synthetic LAN
  (`WebRTCLan`); a configured `IpDrv.TcpNetDriver` is replaced at startup. The package, class and `NetworkDevice`
  value keep their names.
- **Android** is the SDL client as a shared library (`UE_ANDROID_CLIENT` in CMake, `UE_ANDROID_APP` in the code
  for the app's own paths: the game folder from `UT99_ANDROID_BASEDIR`, the log on logcat); `__ANDROID__` guards
  what every Android build shares. Touch stays a mouse for the menus; the overlay's input enters through
  `Source/NSDLDrv/Src/NSDLAndroid.cpp`. The Android inis are generated (`tools/dev/sync-android-ini.py`), never
  edited by hand.
- **Render device at startup:** a `-*RENDERER` switch, else the persisted choice when it is a hardware default or
  the user's marked pick (`[Engine.Engine] ufrontUserRenderDevice`), else XOpenGL, UTGLR, NOpenGLES (on Android
  NOpenGLES, then XOpenGL).

## This branch: v436 compatibility
- **Version.** `Core/Inc/UnObjVer.h`: `ENGINE_VERSION` 436 (behind `#ifndef`, so a translation unit may override it),
  `ENGINE_MIN_NET_VERSION` 400, `PACKAGE_FILE_VERSION` 69 with a licensee half (`ArLicenseeVer`); loading accepts
  file version 60 upward. The fetched XOpenGLDrv compiles with `ENGINE_VERSION=400`
  (`Source/XOpenGLDrv/CMakeLists.txt`): its render-header paths key on 400 and the render structs it uses are not
  version-gated.
- **Layouts and natives follow the retail v436 packages.** A C++ class or struct that a script class mirrors must
  match v436's `Engine.u`/`Editor.u`/`IpDrv.u` (the layout guard is fatal at load); a native function keeps v436's
  number. At compile time `CHECK_NATIVE_REPLICATION` (`Engine/Src/UnActor.cpp`) ties `CLASS_NativeReplication` to
  declaring `GetOptimizedRepList`, and at load `UClass::Serialize` warns `[netrep]` for a `nativereplication` script
  class without one.
- **Net behaviour is retail v436's** (replication lists, priorities, the login handshake with its 432 floor, the
  float-to-int rounding sites through `appRound`): a change there decides whether a v436 server or client still
  talks to this engine.
- **469 string natives.** `Locs` 238, `Repl` 239, `$=` 322, `@=` 323, `-=` 324 (`Core/Src/UnCorSc.cpp`) at OldUnreal
  469's numbers, because a 469-compiled mod calls a final native by its number. `ClearConfig` 537, `AddToPackageMap`
  1718 and `IsInPackageMap` 1719 touch engine state and stay unregistered. `tools/oracle-469-natives/run.sh
  <469-install> <436-run-tree>` checks them against stock 469.
- **Downloads.** `Engine/Src/UnDownload.cpp` (`UDownload`, `UChannelDownload`) and `IpDrv/Src/HTTPDownload.cpp`.
  A `.uz` file's codec chain is RLE, BWT, MTF, a second RLE only for signature 5678, then Huffman on every file. On
  the web `UHTTPDownload` fetches through the `GWebHttp` hook table (`IpDrv/Inc/WebHttp.h`); with no hook it fails at
  once and the next download manager takes over. Nothing may follow `DownloadDone()`: it can start the next
  download, which deletes the object.
- **Package lookup by GUID.** On a join `appFindPackageFile` gets the server's GUID and size: a candidate of that size
  is taken without opening it, another size is checked by its GUID (natively the package summary, on the web the
  page's manifest through the optional pkgguid module, `vendor/glue/ue_pkgguid_abi.h`); `.u` files are checked too.
  A package already loaded in another version cannot be swapped, so `UNetPendingLevel` refuses that join at once.
- **ProxyNetDrv** (`Source/ProxyNetDrv/`, web only, experimental, `UE_WITH_PROXYNET`): a client-only net driver over
  the optional proxynet module (`ue_proxynet_*`). It shares nothing with WebRTCDrv. IpDrv imports nothing from
  JavaScript: ProxyNetDrv registers the generic `GWebHttp` (redirect downloads, at its first connect) and `GWebLinks`
  (`IpDrv/Inc/WebLinks.h`, the in-game server browser, at startup) tables, and `ATcpLink`/`AUdpLink` hand the
  connections a browser cannot make to `GWebLinks`. UBrowser's script is unchanged. The launcher keeps
  `NetworkDevice=ProxyNetDrv.ProxyNetDriver` because the build has it.
- **WebRTC peer prefix** `ut99-436-compat` (`Source/WebRTCDrv/WebRTCDrv.cpp`) must match the frontend's
  `targets/ut99-436-compat/target.json` `network.peerPrefix`; it keeps a v436 client off a v400 host.

## Rules
- **Comments** explain the code as it is, in one or two lines. No stage markers or work ids, no story of a fix,
  no where-a-report-came-from, no owner quotes, dates or commit hashes, no cross-references to other files' or
  repos' comments, no glyphs or capitalised emphasis, no platform asides in shared code (a platform statement
  lives inside its `#ifdef`). A comment that disagrees with the code is a bug: fix one of them. Epic's and
  ut99dc's comments are theirs: leave them. Log text is not a comment.
- **Docs** (README.md, BUILD.md, this file, `Config/System/web/README.md`, `Source/OpenGLDrv/{PORTING,
  WEB-INTEGRATION}.md`, `Source/XOpenGLDrv/{ENGINE_CHANGES,WEB_INTEGRATION}.md`, `Source/SoftDrv/WEB-NOTES.md`)
  describe the current state in present tense: no phases, no history, no dates, no owner attributions, no
  measurements as proof. Rewrite from the code; every path, flag, preset and script a doc names must exist.
- **Names** follow behaviour. Shared CMake options take the `UE_` prefix; the `UTGLR_*` gates are the fork's own.
  Do not rename ini sections or keys players may have saved (e.g. `ufrontUserRenderDevice`), exported ABI symbols,
  or log lines the frontend matches. "ufront" as a namespace stays (`[ufront.AutoFOV]`, `ufront_pacing`).
- Commit subjects name the feature (Conventional Commits), never a stage number or a date.
- The Dreamcast target's code (KOSDrv, PVRDrv, AICADrv, DCUtil, the FATFS paths, the stdio file pool,
  `LOW_MEMORY`) stays on that target; do not take Android device workarounds from UT99-Android.
- Never commit game assets or build trees (`build-*/`, gitignored). Do not push to maximqaxd/ut99dc.
- Before a commit, `node <frontend>/.ufront/hygiene/scan.mjs --all --repo .` must report no hard findings.
