# Building engine-ut99

The engine source of Unreal Tournament (1999), Unreal Engine 1 v400, for the browser and for native Linux, Windows
and Android. The web build produces a WebAssembly artifact that the frontend repository
([10-p/unreal-frontend-arch-private](https://github.com/10-p/unreal-frontend-arch-private)) serves; game assets (a
retail UT99 v400 install) live outside this repository and no build reads them. The base is
[maximqaxd/ut99dc](https://github.com/maximqaxd/ut99dc) at `820b25c`; `feat/v436-compat` is the same engine for v436
content, rebased on `main`.

## Presets

All presets are in `Source/CMakePresets.json`; run `cmake --preset …` from `Source/`. Build trees go to the repo root
(`build-<preset>/`, gitignored).

| Preset | Output | What it is |
|---|---|---|
| `native` | `build-native/UnrealTournament/UnrealTournament` + `libopenal.so.1`, `build-native/UCC/ucc-bin` | 32-bit Linux, one static binary (needs a 32-bit SDL2) |
| `native64` | `build-native64/…` | the same at x86_64 (LP64); needs the amd64 SDL2 development files (Debian `libsdl2-dev:amd64`, Arch `sdl2-compat`) |
| `android-arm64` | `build-android-arm64-client/UnrealTournament/libUT99.so` + `_deps/sdl2-build/libSDL2.so`, `libut99_android_bridge.so` | the game as one shared library for Android arm64 (API 30, shared libc++; needs `ANDROID_NDK_HOME`): the configuration the Gradle project in `Android/` builds into the APK |
| `android-arm64-ucc` | `build-android-arm64/UCC/ucc-bin` | a headless `ucc-bin` for Android arm64 (bionic, API 24, static libc++; needs `ANDROID_NDK_HOME`) |
| `android-armv7-ucc` | `build-android-armv7/UCC/ucc-bin` | the same for armeabi-v7a |
| `windows` | `build-windows/UnrealTournament/UnrealTournament.exe` + `OpenAL32.dll`, `build-windows/UCC/ucc.exe` | 32-bit Windows, MinGW-w64 (cross or MSYS2) |
| `windows64` | the same in `build-windows64/` | 64-bit Windows, MinGW-w64 (x86_64), the shipped 32-bit `.u` unchanged |
| `wasm` | `build-wasm/SDLLaunch/` | web, the development build: -O2 with DWARF and assertions |
| `wasm-debug` | `build-wasm-debug/SDLLaunch/` | web at -O0, for a bug the optimiser is suspected of |
| `wasm-perf` | `build-wasm-perf/SDLLaunch/` | web, the shipping build: -O2 compile, -O3 -g0 link, no assertions, a symbol map beside the loader |

```bash
cd Source
source <emsdk>/emsdk_env.sh                                                  # emsdk 5.0.7; sets $EMSDK
cmake --preset wasm-perf && cmake --build --preset wasm-perf -j"$(nproc)"   # the web build
cmake --preset native    && cmake --build --preset native    -j"$(nproc)"
cmake --preset windows   && cmake --build --preset windows   -j"$(nproc)"
```

Every preset is `BUILD_STATIC`: all packages link into one executable (`--whole-archive`, `UNREAL_STATIC`), and
`appGetDllHandle` resolves a package through the internal export table (`Source/Core/Src/UnStaticExports.cpp`)
instead of `dlopen`/`LoadLibrary`. Every playable preset builds the stripped, GUI-less Editor package: maps and meshes
reference its classes at run time. `ucc-bin` (dedicated server, `make`, commandlets) is built on every native target;
it links the headless package set (Core, Engine, Render, IpDrv, UWeb, Fire, Editor) and no client, renderer or audio.

## CMake options

| Option | Default | Meaning |
|---|---|---|
| `UE_WEB_PERF` | OFF | web: the shipping link (-O3 -g0, no assertions, `--emit-symbol-map`) |
| `UE_WEB_DEBUG` | OFF | web: compile at -O0 instead of -O2 |
| `UE_BUNDLED_OPENAL` | ON | native Linux: build openal-soft and ship `libopenal.so.1` beside the binary instead of linking the system libopenal |
| `UE_ALLOW_64BIT` | OFF | build x86_64 as a 64-bit engine; OFF demotes an x86_64 compiler to -m32 (an arm64 compiler always builds 64-bit; 64-bit MSVC is refused) |
| `BUILD_STATIC` | OFF (ON in every preset) | one binary |
| `BUILD_EDITOR` | ON | the Editor package |
| `BUILD_UNREALTOURNAMENT` | ON | the game launcher (`android-*-ucc` turn it off) |
| `USE_SDL` | ON | the SDL2 client (NSDLDrv); off for the Dreamcast's KOSDrv and the headless `android-*-ucc` presets. On Android with it on, the playable set is forced (below) |
| `BUILD_XOPENGLDRV`, `BUILD_UTGLR`, `BUILD_NOPENGLESDRV` | OFF, OFF, ON (the presets turn on what they build) | the hardware render devices: XOpenGLDrv, OpenGLDrv/UTGLR, NOpenGLESDrv |
| `USE_GL4ES` | OFF (ON on the web) | build UTGLR against gl4es (desktop GL → GLES2); implies `BUILD_UTGLR` |
| `BUILD_SOFTDRV`, `BUILD_NULLDRV` | ON, ON | the software and null render devices |
| `BUILD_NOPENALDRV`, `BUILD_NULLSOUNDDRV` | OFF, ON (both ON in the playable presets) | audio: OpenAL Soft + libxmp, and the silent subsystem |
| `SOFTDRV_MAX_SCREEN` | 4096 (8192 on the web presets) | the bound of SoftDrv's screen-sized tables |
| `BUILD_DCUTIL` | ON (every preset turns it off) | the Dreamcast utility package |
| `BUILD_PVRDRV`, `BUILD_AICADRV`, `LOW_MEMORY`, `DREAMCAST_*` | OFF (`DREAMCAST_USE_FILE_POOL` ON) | the Dreamcast target (no preset builds it) |

The web (`if(EMSCRIPTEN)` in `Source/CMakeLists.txt`) forces its own set: static, SDL, Editor, XOpenGLDrv, UTGLR
through gl4es, NOpenGLESDrv, SoftDrv, NullDrv, NOpenALDrv, the silent audio subsystem, `ASM=0`, `-pthread` and
`-sUSE_SDL=2` at compile time, `-fpack-struct=4`. The Android client (`if(ANDROID AND USE_SDL)`) forces static,
Editor, XOpenGLDrv, NOpenGLESDrv, SoftDrv, NullDrv, NOpenALDrv and the silent audio subsystem, no UTGLR (desktop
GL) and no `ucc-bin`, defines `UE_ANDROID_APP` for the app's own platform paths, and adds
`-fno-delete-null-pointer-checks` and clang's non-POD-varargs leniency.

## Fetched libraries

The first configure of a preset fetches (FetchContent; the cache variables name the repository and the ref):

| Library | Default | Variables | Used by |
|---|---|---|---|
| XOpenGLDrv | [10-p/XOpenGLDrv](https://github.com/10-p/XOpenGLDrv), branch `ufront` | `XOPENGL_GIT_REPOSITORY`, `XOPENGL_GIT_TAG` | `Source/XOpenGLDrv` (build glue) |
| etcpak | [wolfpld/etcpak](https://github.com/wolfpld/etcpak), commit `3d716e1` | `ETCPAK_GIT_REPOSITORY`, `ETCPAK_GIT_TAG` | XOpenGLDrv's ETC2 encoder (`Source/XOpenGLDrv/ENGINE_CHANGES.md`, "Textures") |
| astc-encoder | [ARM-software/astc-encoder](https://github.com/ARM-software/astc-encoder), 5.7.0 (`baff485`) | `ASTCENC_GIT_REPOSITORY`, `ASTCENC_GIT_TAG` | XOpenGLDrv's ASTC encoder (`Source/XOpenGLDrv/ENGINE_CHANGES.md`, "Textures") |
| OpenGLDrv/UTGLR | [10-p/utglr-copy](https://github.com/10-p/utglr-copy), branch `main` | `UTGLR_GIT_REPOSITORY`, `UTGLR_GIT_TAG` | `Source/OpenGLDrv` (build glue) |
| gl4es | [10-p/gl4es](https://github.com/10-p/gl4es) (a fork of ptitSeb/gl4es), branch `ufront` | `GL4ES_GIT_REPOSITORY`, `GL4ES_GIT_TAG`, `GL4ES_SOURCE_DIR` | UTGLR on the web |
| openal-soft | [10-p/openal-soft-web](https://github.com/10-p/openal-soft-web), branch `ufront/web` | `OPENALSOFT_GIT_REPOSITORY`, `OPENALSOFT_GIT_TAG` | NOpenALDrv: static on the web, `libopenal.so.1` on native Linux (`Source/cmake/OpenALSoft.cmake`) |
| libxmp | `libxmp-4.7.1` | `LIBXMP_GIT_REPOSITORY`, `LIBXMP_GIT_TAG`, `LIBXMP_SOURCE_DIR` | NOpenALDrv (`Source/NOpenALDrv/CMakeLists.txt`) |
| SDL2 | [libsdl-org/SDL](https://github.com/libsdl-org/SDL), branch `release-2.32.x` | `SDL2_GIT_REPOSITORY`, `SDL2_GIT_TAG`, `FETCHCONTENT_SOURCE_DIR_SDL2` | the Android client, built shared as `libSDL2.so` (the Gradle project passes its own checkout, whose Java it compiles) |
| SDL2, OpenAL Soft (Windows binaries) | 2.32.10, 1.25.2 | `SDL2_MINGW_VERSION`, `OPENAL_MINGW_VERSION` | `windows` when cross-compiling (`Source/cmake/SDL2Mingw.cmake`, `OpenALMingw.cmake`) |

A branch ref is re-fetched on every configure (`cmake --preset …`), never by a plain `cmake --build`. To build a local
checkout, pass `-DFETCHCONTENT_SOURCE_DIR_XOPENGLDRV=<path>` or `-DFETCHCONTENT_SOURCE_DIR_UTGLR=<path>`. The two
forks' own changes for this engine are described beside their build glue (`Source/XOpenGLDrv/ENGINE_CHANGES.md`,
`WEB_INTEGRATION.md`; `Source/OpenGLDrv/PORTING.md`, `WEB-INTEGRATION.md`).

## The web artifact

`build-wasm*/SDLLaunch/` holds a bare loader, `SDLLaunch.{js,wasm,data}`, and `meta.json`. It is not playable on its
own: the frontend's build validates `meta.json` and serves the loader from its host page, which injects the file
system and the network transport at run time.

- **Threads.** `main()` runs on a pthread (`-sPROXY_TO_PTHREAD`), and `#canvas` is transferred to it
  (`-sOFFSCREENCANVASES_TO_PTHREAD`). NSDLViewport creates the WebGL context on that worker and presents each frame
  with `emscripten_webgl_commit_frame` (`-sOFFSCREEN_FRAMEBUFFER`); `offscreen-depth24.sh` rewrites the emitted
  loader so that framebuffer's depth is D24S8. A software frame goes to the canvas through a 2D context.
- **WebGL.** Every GL device gets a WebGL 2 context; NOpenGLESDrv and UTGLR take WebGL 1 with `-WEBGL=1` or when the
  browser has no WebGL 2. `Source/UnrealTournament/msaa-backbuffer.js` (`ufront_backbuffer_samples`) gives the devices
  that draw into framebuffer 0 a multisampled back buffer.
- **Files.** WasmFS with a JS-implemented backend (`vendor/glue/wasmfs-shim.js`) forwards file operations to the host.
  `Config/System/web/{Default,DefUser}.ini` are preloaded into `/System`; the launcher `chdir`s there and
  `appBaseDir()` is the working directory.
- **Main loop.** Each frame returns to the worker's event loop through Asyncify (`ufront_wait_frame`, an
  `EM_ASYNC_JS` wait) and resumes at the display's next frame; with a frame-rate cap, at the first display frame its
  due time reaches; from a timer while the page is hidden, or with `-TIMERFRAMES`. Each frame is recorded in a ring
  that the console command `ufront_pacing` dumps as CSV.
- **Exceptions.** The engine's objects do not catch exceptions on the web (only openal-soft's objects are built with
  `-fexceptions`), so loading fails soft there: `GetPackageLinker`, `StaticLoadObject`, `SafeLoadError`, a missing
  nested import and `LoadMap` resolve to NULL or a failed join, and a bad startup map falls back to the menu.
  `appThrowf` writes `[appThrowf] <reason>` to stdout on every build.
- **Glue ABI.** `vendor/glue/` (`ue_abi.h`, `net-shim.js`, `wasmfs-shim.js`) is vendored from the frontend at ABI
  version 2; `meta.json`'s `abiVersion` is read from `ue_abi.h`. WebRTCDrv calls the `ue_net_*` imports; the engine
  exports `ue_exec_command`, which runs a console command and returns its output.
- **`meta.json`** records the ABI version, the target, the git commit and build time, the Emscripten version, the
  link flags and every fetched library as `<repo>@<branch|tag>` (a local checkout as `local:<path>`), with checksums
  of the loader files (`Source/UnrealTournament/cmake/emit-artifact-meta.mjs`, `Source/cmake/ArtifactDeps.cmake`).

## Run time

- **Subsystems.** Before `Engine->Init()` the launcher replaces a viewport client, audio subsystem or net driver
  the ini names but the build lacks (the retail `WinDrv.WindowsClient`, `Galaxy.GalaxyAudioSubsystem`) with the first
  present one of its list, and logs it; on the web a stock `IpDrv.TcpNetDriver` becomes `WebRTCDrv.WebRTCNetDriver`.
- **Render device.** `-XOPENGLRENDERER`, `-UTGLRRENDERER`, `-NOPENGLESRENDERER`, `-SOFTWARERENDERER`,
  `-NULLRENDERER` select a device; without a switch the persisted device is kept when it is a hardware default or the
  user's marked pick (`[Engine.Engine] ufrontUserRenderDevice`), else XOpenGL, UTGLR, NOpenGLES in that order
  (on Android NOpenGLES, then XOpenGL). A switch whose device survives Init is persisted.
- **Settings from outside.** `-ini:<File>:[<Section>]:<Key>=<Value>` (`User`/`System` name the user and system inis)
  sets any config key right after the config cache loads, and the value is written into the ini.
- **Frame cap.** `[Engine.Engine] FrameRateCap` (120; 0 or less is uncapped) when the engine sets no max tick rate;
  `ULevel::Tick` clamps a step to 0.001..0.40 s.
- **AutoFOV.** `[ufront.AutoFOV]` `AutoFOV` (0 off, 1 Hor+, 2 Adaptive: the default), `ReferenceAspect`, `WeaponScale`,
  `AutoFOVUI`, `UIScale`: an aspect-correct field of view and HUD scale, logged with `-UFRONTLOG` as `[AutoFOV]` /
  `[AutoFOVUI]`.
- **`-UFRONTLOG`.** Also logs the port's diagnostic and repeated lines (`GUfrontLog`): the `[AutoFOV]` and
  `[AutoFOVUI]` lines, the `[webmouse]` capture and `the menu cursor follows` hand-overs, the `[WebRTC-LAN]` discovery
  rounds, the first reverb zone, and the device information of every render device after the process's first (a
  fullscreen switch re-creates it): the GL strings, XOpenGLDrv's extension list, capability lines, VSync state,
  flushes and shader-compile messages, UTGLR's `GL_EXTENSIONS`, `Device supports` lines and buffer and texture
  limits. The tests pass it; a player's log leaves them out. Status lines, warnings, errors
  and console-command answers are always logged.
- **Console commands.** `FLUSHCONFIG` writes the config cache to disk; `WHEREAMI` and `TELEPORT <x> <y> <z> [<yaw>
  [<pitch>]]` (standalone and listen host; a client needs `[Engine.GameEngine] AllowNetCheats=True`, a test setting);
  `FPSLOG <seconds>` or `[Engine.GameEngine] FpsLogSeconds` for an `FPS:` line per interval.
- **Game pads.** `[NSDLDrv.NSDLClient] UseJoystick` (on) is read every tick, so the game's own option opens or closes
  the pad at once (`NSDLClient: game pads on|off (UseJoystick)`); the first pad connected is used, with hot-plug
  (`NSDLClient: game pad connected: <name>`). Sticks and triggers are read from the pad's state every tick. On the web
  the press that made the browser list the pad is read from `navigator.getGamepads()` at the connect and given to the
  game as a tap. The pad's buttons are the `Joy` keys, and `Config/System/web/DefUser.ini` binds them to Unreal 1's
  pad layout: Back (`Joy5`) shows the scores, Start (`Joy7`) opens UWindow with `MenuCmd 0 -1` (UT99 has no menu for
  `ShowMenu`, which would only pause the game). The stick scales `ScaleXYZ`/`ScaleRUV` default to Unreal 1's 100/100,
  which a retail `User.ini`'s pad bindings (speed 2 and 5.9) are turned with; the web and Android inis set 200/500 for
  `DefUser.ini`'s speed 1. In play the left stick is the move stick: its dead zone (`DeadZoneXYZ`, 0.1) is radial, so
  every direction between the axes keeps both of its parts, and a tilt out of it but below
  `[NSDLDrv.NSDLClient] PadWalkTilt` (0.5 of the stick's travel; 0 never) walks, as the Walking button does: the stick keeps a bit of its own set in the player's
  `bRun` (the Walking key writes the whole byte; the stick sets its bit again the next tick), and the moves a client
  sends to its server carry `bRun`. The right stick keeps a dead zone per axis (`DeadZoneRUV`). While UWindow shows
  its cursor, any pad press or a stick pushed out of its dead zone gives the cursor to the pad, and that press already
  acts as below. While the pad has the cursor, and only then, UWindow draws its own cursor where the pad puts it (the
  windowed viewport's `bWindowsMouseAvailable` is off, as on a fullscreen one) and the system pointer is hidden; the
  left stick moves it (linear from the dead zone's edge, across the viewport's width in `PadCursorSeconds` at full
  tilt: 3), Back and B are Escape, Start is Enter, A is a left click under the cursor shown when that is on a control
  (a grid's rows too: a server list's), else a click at the centre of the focused control (a button presses, a
  checkbox toggles, a drop-down list opens) or, for the controls that take it (pull-down menus, editable boxes,
  message boxes), Enter, the D-pad is the arrow keys, an arrow the focused control does not use moving the focus to
  the next (down, right) or the previous (up, left) control that takes it (`GPadMenuKeys` in `NSDLViewport.cpp`; in
  an open drop-down list the D-pad selects, scrolling the row into view, A and Start pick, Back and B close it; a
  message box takes A and Start as its Yes or OK button and Back and B as its Cancel or No button, a box with OK alone
  keeping Escape), and the keys bound to `Fire` and `AltFire` click left and right (`GPadCursorClicks`); a key-binding
  page waiting for a key gets the pad's own keys, Back aside. The mouse's own motion (a touch's on Android) takes the
  cursor back, and with it a windowed viewport's cursor is the system pointer again and every pad key is itself again
  (`NSDLClient: the menu cursor follows the pad|mouse` with `-UFRONTLOG`).
- **Antialiasing.** XOpenGLDrv multisamples in framebuffers of its own (`UseAA`). NOpenGLESDrv (`NumAASamples`)
  and UTGLR (`UseAA`, `NumAASamples`) draw into framebuffer 0; on the web each asks `ufront_backbuffer_samples` for
  a multisampled back buffer at Init and logs `GLES2: antialiasing 4x (multisampled back buffer)` / `GLES2:
  antialiasing off` (UTGLR: `AA: 4x multisampled back buffer` / `AA: off`). WebGL 1 has none.
- **Command line.** The launcher, UCC and Core's `GCmdLine` hold 4,096 characters; a token that does not fit is
  dropped whole, with a warning once the log is up.
- **Files.** `FindFiles` matches as Windows' `FindFirstFile` does (`*`/`?` anywhere, case-insensitive, `*.*` is
  everything, the Files/Directories flags honoured; `Core/Inc/FFileManagerLinux.h`). `ucc make` saves are
  deterministic: `SavePackage` sorts names, imports and exports with a stable merge sort, and `CacheDrivers` reads
  the `.int` files in NTFS order (`Core/Src/UnObj.cpp`).
- **Script overrides.** A script function listed in `GScriptOverrides` (`Core/Src/UnScriptOverride.cpp`) runs as C++
  in place of its UnrealScript. When its package loads, `UFunction::Bind` binds it as a native, as for a script
  `native` function, if it is exactly the listed one: its export in the package file has the listed size and CRC, and
  its parameters are the ones the C++ reads. The editor and `ucc` keep the script, and so does a mod's or another
  release's version of the function. The log says which: `ufront: <function> runs as C++: ...` or `... keeps its
  script: ...`. The one entry is UTMenu's `UTLadderStub.SetupWinParams`, which places every ladder window (character
  creation, the ladders, the save slots, the manager, the objectives): Epic's box (1024x768, else the root's height at
  4:3) where it fits UWindow's root, else the largest 4:3 box that fits, so the whole window shows with the 2x menus on
  a wide screen (a 1170x540 root at 2340x1080) or the 1x menus at 1280x720.
- **Signals.** On POSIX targets SIGTERM, SIGHUP and SIGINT request a clean exit; on Windows the console's Ctrl-C,
  Ctrl-Break and close do (a dedicated server under `ucc.exe`). A second one is reported and ignored.
- **Logging.** Every log line is echoed to stdout (the browser console on the web); `Init: Language: <ext>` names the
  localization in use.

## 64-bit (LP64)

`native64` and the `android-*` presets build a 64-bit engine that runs the retail 32-bit `.u`
packages unchanged. `PLATFORM_64BITS` comes from the compiler (`Core/Inc/Core.h`).

- Script object references are 4-byte object indices in memory (`UObject::GetScriptObject` / `GetScriptRef`;
  `UStruct::SerializeExpr` converts on load and save), so no bytecode offset moves; multi-byte operands are read by
  copy (`ReadScriptOperand`).
- Every header declaring a scripted native class or a script-mirrored struct is `#pragma pack(push,4)`, the generated
  `*Classes.h` included; object properties align to 4; `NativePlaceholderSize` (`Core/Src/UnClass.cpp`) gives the
  script `int` stand-ins for C++ pointers their C++ size. A C++/script size mismatch of a native class is a fatal
  error at load.
- A None context zero-fills its whole destination; the script compiler records the 32-bit format's sizes
  (`ScriptFormatStructSize`), so a 64-bit `ucc make` writes the same package as a 32-bit one but for the GUID.
- `PTRINT`/`UPTRINT` wherever a pointer passes through an integer; a POSIX build without SDL keeps time with
  `CLOCK_MONOTONIC` and finds its directory through `/proc/self/exe`; ARM gets `-fsigned-char`.
- A size check is not an offset check: GCC and clang lay out a derived class's first member in its base's tail
  padding, pack a `BYTE` after bitfields into the bitfield unit and a bitfield after `BYTE`s into theirs, and align
  a four-`BYTE` `FColor` to 1. Each such member carries `GCC_ALIGN(4)`/`GCC_PACK(4)` in its header (empty on MSVC).
  `tools/dev/layout-sweep.gdb` checks every offset; run it under `ucc-bin make` and under the client, since some
  reflected layouts (the input alias struct) exist only where there is a client.

On a phone: push the asset tree to `/data/local/tmp/ut99/` and `ucc-bin` beside it, run the server from
`adb shell`, and join from a desktop client by the phone's address (UDP cannot be `adb forward`ed).

## Android

`Android/` is a Gradle/NDK project (Android Gradle plugin 9.4.0, Gradle 9.6.0 through the wrapper, its daemon on
an installed JDK 17) whose `externalNativeBuild` points at `Source/CMakeLists.txt` (targets `UnrealTournament`,
the game as `libUT99.so`, and `ut99_android_bridge`), built for `armeabi-v7a`, `arm64-v8a` and `x86_64`
in one APK (x86 Chromebooks and the emulator run the last). It needs platform 36 (`compileSdk`; `targetSdk` 34,
`minSdk` 30), build-tools 36.0.0, NDK 28.2.13676358 and CMake 4.1.2 or newer. Ubuntu's packages install the SDK
into `/usr/lib/android-sdk`; `local.properties` names it and the distribution's CMake:

```bash
sudo apt install openjdk-17-jdk cmake ninja-build adb google-android-cmdline-tools-19.0-installer \
  google-android-platform-36-installer google-android-build-tools-36.0.0-installer google-android-ndk-r28c-installer
printf 'sdk.dir=/usr/lib/android-sdk\ncmake.dir=/usr\n' > Android/local.properties
```

```bash
cd Android
./gradlew :app:assembleRelease      # -> app/build/outputs/apk/release/app-release.apk (assembleDebug: debuggable)
#   -Psdl2Dir=<path>     an SDL2 checkout (release-2.32.x); else one is cloned into Android/build/SDL2
#   -PnativeDebug        the engine at the variant's own CMAKE_BUILD_TYPE (every variant is RelWithDebInfo otherwise)
adb install -r app/build/outputs/apk/release/app-release.apk
```

- Signing: every variant is signed with the key the Gradle property `ueAndroidSigning` names, a properties file kept
  outside the repository (`storeFile`, `storePassword`, `keyAlias`, `keyPassword`), set in `~/.gradle/gradle.properties`
  or as `UE_ANDROID_SIGNING`. One key signs every release, so each APK installs over the last; without the property
  the build uses the debug key.
- The engine version (`ENGINE_VERSION`, `Source/Core/Inc/UnObjVer.h`) names the app: v400 is `com.ut99.android`,
  "Unreal Tournament", game folder `UT99`; v436 (`feat/v436-compat`) is `com.ut99.android.v436`, "Unreal Tournament
  v436", game folder `UT99-436`. The two install side by side.
- SDL2 is one checkout for both halves: the C library the engine's CMake builds (`FETCHCONTENT_SOURCE_DIR_SDL2`,
  shared, `libSDL2.so`) and the Java `SDLActivity` compiled from `android-project/`.
- openal-soft (OpenSL ES, no C++20 modules, no RTKit) and libxmp are linked statically into `libUT99.so`.
- The engine runs on a thread of its own with an 8 MB stack (`SDL_main`, `Source/UnrealTournament/Src/Launch.cpp`);
  the log is on logcat (tag `UT99`, with stdout and stderr as `UT99-stdio` and the bridge as `UT99Bridge`) and in
  the game folder's `System/UnrealTournament.log`.
- The inis: `tools/dev/sync-android-ini.py` writes `Config/System/android/{Default,DefUser}.ini` from the web
  pair (`--check` verifies them); the APK carries them as assets, and the game installs them into the folder's
  `System/` before each start when they differ. The engine derives `UnrealTournament.ini` and `User.ini` from
  them when those are missing.

The engine reads its game folder (a retail install: `System/`, `Maps/`, `Textures/`, ...) by path; the bridge
(`Android/app/src/main/cpp/android_bridge.cpp`) sets `UT99_ANDROID_BASEDIR`, `HOME` and the working directory
before SDL starts `SDL_main`, and the launcher (`MainActivity`) chooses which folder:

- Internal storage (`/data/data/com.ut99.android/files/UT99`): fast, deleted with the app, no permission. "Choose
  the UT99 folder" opens Android's folder picker, which gives the app that folder only, and "Copy to internal
  storage" copies it in (only what is new or changed: a stopped copy resumes); with no folder chosen yet it opens
  the picker first. "Copy settings and saves" writes the internal copy's `System/UnrealTournament.ini`,
  `System/User.ini` and `Save/` back to the chosen folder.
- A UT99 folder in place: `Android/data/com.ut99.android/files/UT99` on shared storage or an SD card needs no
  permission; `/sdcard/UT99` or `UT99` at the top of an SD card needs All files access, which the launcher asks
  for, with the copy as the alternative, only when the game is started from there. Shared storage is a FUSE
  mount and the engine's case-insensitive file lookup is slow there; the internal copy is the fast choice.
- The launcher's resolution scale (a slider from 0.25 to 1 and a field that takes 0.1 to 1; 1, the screen's own
  resolution, by default) sizes SDL's surface buffer (`GameActivity.applyRenderScale`, `SurfaceHolder.setFixedSize`),
  which Android scales to the screen; the engine sees the scaled size as its window. SDL's surface view
  (`FrameSurface`) scales every touch and mouse position from the screen to that buffer before SDL reads it, so a
  tap lands where it is drawn at every scale.

Controls: a pad through NSDLDrv's game-controller path (`UseJoystick`, the pad layout of `DefUser.ini`; in the
menus, once the pad has the cursor, its left stick moves it, its fire buttons click and A, B and the D-pad are
Enter, Escape and the arrows); touch as the mouse in the menus (tap to click); in play an on-screen overlay (a move stick on the left half, drag to
look on the right, FIRE, ALTFIRE, JUMP, CROUCH, NEXT and MENU buttons) that drives the engine through
`Source/NSDLDrv/Src/NSDLAndroid.cpp`. The move stick moves in the drag's direction and tilts fully as the launcher's
"Touch move stick" settings say: full speed at a drag of 12 dp from where the finger is held (4 to 60) and a dead
zone of 3 dp for a resting finger (0 to 10, always below full speed), saved with the launcher's other settings and
handed to the game when it starts (`GameActivity.EXTRA_MOVE_FULL_TILT_DP`, `EXTRA_MOVE_DEAD_ZONE_DP`); past full tilt
the held point follows the finger. It never walks. The system's Back key is Escape.

A travel URL can be passed from adb; the launcher's countdown then starts the game without a touch:

```bash
adb shell am start -n com.ut99.android/.MainActivity --es com.ut99.android.ARGS 'DM-Deck16][?Game=Botpack.DeathMatchPlus'
adb logcat -s UT99 UT99-stdio UT99Bridge SDL
```

`GameActivity` is not exported; launch `MainActivity`. The same native configuration without the APK is the
`android-arm64` preset (`ANDROID_NDK_HOME` set; pass `-DFETCHCONTENT_SOURCE_DIR_SDL2=<checkout>` to reuse the
Gradle project's SDL2).

## Windows (MinGW-w64)

One `.exe` with `OpenAL32.dll` beside it: libgcc, libstdc++ and winpthread are linked statically, and CI checks
that the exe imports only system DLLs. The toolchain file (`Source/cmake/toolchain-mingw-w64.cmake`) targets
`i686-w64-mingw32` for `windows`, the compiler MSYS2's `MINGW32` environment ships, so the same preset works from an
MSYS2 prompt (where pacman's SDL2 answers), and `x86_64-w64-mingw32` for `windows64` (LLP64: `long` stays 4 bytes,
pointers are 8). Renderers: XOpenGLDrv and UTGLR through the system `opengl32`, SoftDrv and NullDrv;
NOpenGLESDrv is off.

The platform layer is chosen by compiler: MSVC gets `Core/Src/UnVcWin32.cpp`, every GCC/Clang target
`Core/Src/UnUnix.cpp`, with MinGW's few Win32-only calls in `Core/Src/UnMinGW.cpp`. Windows-specific code keys on
`PLATFORM_WIN32` (IpDrv on `__WINSOCK__`/`__BSD_SOCKETS__`), not on the compiler. The 32-bit build compiles every
source with `-fpack-struct=4` (MinGW aligns doubles to 8; UE1 assumes `/Zp4`); the 64-bit one leaves the Win64 API's
structs alone and relies on the script-mirrored headers' own `#pragma pack(push,4)`. `ABrush`'s bitfield pad is skipped
under MinGW's MS bitfields. At 64 bits a WinSock `SOCKET` is 8 bytes: `InternetLink`'s script `int Socket` holds it
truncated and `AInternetLink::GetSocket()` reads it back sign-extended (a WinSock handle has 32 significant bits).

Checking without Windows, with `wine` and `Xvfb`; SoftDrv and NullDrv need no GPU:

```bash
Xvfb :99 -screen 0 1024x768x24 &
export WINEDLLOVERRIDES="mscoree,mshtml="   # a new prefix otherwise waits on the Mono/Gecko installer dialogs
DISPLAY=:99 WINEPREFIX=<prefix> wine UnrealTournament.exe -SOFTWARERENDERER   # or -NULLRENDERER
WINEPREFIX=<prefix> wineserver -k
```

Copy the `.exe` into the run tree's `System/` (the engine takes its base directory from the binary's path) and copy
the assets rather than symlinking them (`cp -rL`): under wine a symlinked package is a `FileNotFound`.

`ucc.exe` is the console tool, the retail UCC's name, with the system DLLs only: it reads its arguments as `ucc-bin`
does on Linux and writes `ucc.log` (`wine ucc.exe help`, `wine ucc.exe server "DM-Deck16][?game=Botpack.DeathMatchPlus"`),
the menus' dedicated-server relaunch starts it, and a SIGINT to the wine process reaches it as Ctrl-C.

## Test tools

| Tool | What it does |
|---|---|
| `tools/dev/mkrun-ut99.sh <build-dir> <run-dir> [<extra-maps-dir>]` | assembles a run tree from a native build and an asset install (default `<work folder>/assets/ut99-assets`, `$UT99_ASSETS_DIR`; native inis from `<work folder>/native-runtrees/ut99`, `$UT99_REF_RUNTREE`) |
| `tools/dev/run-template.sh` | the run tree's `run.sh`: starts the game on a chosen renderer (`./run.sh soft DM-Barricade`) |
| `UT99_RUN=<run-tree> tools/dev/shot.sh <Map.unr> <out.png>` | runs the native build under Xvfb and grabs a frame (`UT99_DEV`, `UT99_RES`, `UT99_DISP`) |
| `tools/dev/botmatch.sh <server-tree> <seconds> [<client-tree>] [<map>]` | a headless bot match with a joined client; counts kills and crash markers |
| `tools/dev/layout-sweep.gdb` | every script property of every native class (and script struct) against the same-named C++ member; run under `ucc-bin make` and the client |
| `tools/dev/census/` | a coverage census of which engine functions ever ran (`census.py`, `gcovdump.c`, the scripted player session `play.sh`) and a cross-width layout oracle (`exports.sh`) |
| `tools/test/U64Test/` | an UnrealScript fixture for the 64-bit port: VM checks (`U64Mutator`), compile-only declarations (`U64Decl`), a benchmark (`U64Bench`) |
| `tools/dev/inputtest/run.sh <run-tree> <scenario> <out-dir> [<args>]` | runs the native build under Xvfb with the input test channel (`-INPUTTEST=<file>`, `Source/NSDLDrv/Src/NSDLInputTest.h`: a virtual SDL pad that takes the pad's place, mouse motion, console commands, state probes, per-tick view watches) and feeds it a scenario (`turn.scen`; `move.scen`: the move stick's speed against tilt and its directions); `rate.py` fits the turn rate and the speed, and gives the move direction and the walking share, from the log |
| `tools/web-smoke.mjs` | serves a web artifact to headless Chromium; it sends no COOP/COEP headers and sets no `Module.UE`, so it reports the abort of the threaded build (the frontend's E2E is the web gate) |

```bash
tools/dev/mkrun-ut99.sh build-native64 <run-tree>      # then copy UCC/ucc-bin in
cd <run-tree>/System
gdb -batch -x <repo>/tools/dev/layout-sweep.gdb --args ./ucc-bin make          # healthy: only EditorEngine.Pad shifted
./ucc-bin server "DM-Deck16][?game=Botpack.DeathMatchPlus?mutator=U64Test.U64Mutator"   # U64 DONE … failed=0
<repo>/tools/dev/botmatch.sh <server-tree> 300 [<client-tree>] [<map>]          # bots need MinPlayers in the ini
```

The coverage census builds `native64` with `--coverage` and `gcovdump.o` linked in:

```bash
cc -c -O2 -fPIC tools/dev/census/gcovdump.c -o <dir>/gcovdump.o
cd Source && cmake --preset native64 -B ../build-native64-cov -DCMAKE_C_FLAGS=--coverage -DCMAKE_CXX_FLAGS=--coverage \
    "-DCMAKE_EXE_LINKER_FLAGS=--coverage <dir>/gcovdump.o" -DCMAKE_SHARED_LINKER_FLAGS=--coverage && cmake --build ../build-native64-cov
python3 tools/dev/census/census.py build-native64-cov Source build-native64-cov/_deps/{utglr,xopengldrv}-src   # -> census.json
```
