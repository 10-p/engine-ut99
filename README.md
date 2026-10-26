## What?

Unreal Tournament (1999) Unreal Engine 1 v400 source with modifications to make it run on modern systems and in the browser.
Requires assets from a retail UT99 install (v400 packages). For v436 assets, build the `feat/v436-compat` branch the same way.
Based on [maximqaxd/ut99dc](https://github.com/maximqaxd/ut99dc). Build details, options and test tools: [BUILD.md](BUILD.md).

## Changes from the original source

* **WebAssembly build:** every package in one static wasm that runs on a browser worker thread, with WebGL 2, a file system served by the host page, a frame loop that returns to the browser every frame, and load errors that fail soft.
* **SDL2 client driver (NSDLDrv):** the window and input client on every target: a resizable window at the real drawable size, pointer capture in play and an absolute cursor in menus, game pads with hot-plug (in play the bindings of Unreal 1's layout; in the menus, once the pad has the cursor, Back and B as Escape, Start and A as Enter, the D-pad as the arrows and the focus, the left stick and the fire buttons as the mouse, the cursor drawn by UWindow itself).
* **OpenGL ES renderer (NOpenGLESDrv):** a GLES2 device on WebGL 2 and native GL, with antialiasing on the web, anisotropic filtering and the Brightness setting.
* **OpenGL renderers (XOpenGLDrv, UTGLR):** OldUnreal's XOpenGLDrv (the default) and Chris Dohnal's UTGLR, fetched from their forks and built on every target (UTGLR through gl4es on the web).
* **Software renderer (SoftDrv):** Epic's v400 rasterizer through its portable C paths, with volumetric fog on surfaces and meshes, optional bilinear filtering and 2D lines on every compiler.
* **Null renderer (NullDrv):** a render device that draws nothing, for headless runs.
* **WebRTC net driver (WebRTCDrv):** browser multiplayer over the frontend's WebRTC transport, with a synthetic LAN that makes the stock server browser work.
* **OpenAL Soft audio (NOpenALDrv):** one openal-soft build on the web, Linux and Windows, with EFX reverb, optional HRTF and libxmp tracker music.
* **64-bit and ARM support:** x86_64 and arm64 builds that run the retail 32-bit packages unchanged.
* **Android build:** a Gradle project for Android 7.0 and later that builds the game as `libUT99.so` with SDL2, a launcher that picks the game folder, touch as the mouse in the menus and an on-screen overlay in play (hidden while a real pad is used, back on a touch), game pads, a suspended game in the background (no tick, the OpenAL device paused).
* **Windows build:** one static MinGW-w64 executable, cross-compiled from Linux or built under MSYS2.
* **UCC on every desktop build:** `ucc-bin` (Linux) and `ucc.exe` (Windows), 32- and 64-bit, beside the game: the dedicated server and the commandlets.
* **Dreamcast build** (from maximqaxd/ut99dc): kept compiling, built by no preset here.
* **GCC and Clang support:** the engine compiles with GCC, Clang, Emscripten and MinGW-w64, with the native class layouts matching the script packages.
* **Save games:** saving and loading work on case-sensitive file systems and in nested save folders.
* **Languages:** the language is `[Engine.Engine] Language`, and localization files are found case-insensitively.
* **Ladder windows fit the menus:** the single-player ladder's windows show whole at every aspect ratio and menu size, through a C++ version of `UTLadderStub.SetupWinParams` that replaces Epic's own function only (BUILD.md, Script overrides).
* Editor UI is not supported.

The full list, with the medium and small changes, is in the frontend's change inventory
(`<repo>/../unreal-frontend-arch-private/docs/14-change-inventory.md`).

## Building

Builds are 32-bit unless noted and use the CMake presets in `Source/`, so run every `cmake` command from `Source/`.
The first configure of each preset needs network access: the XOpenGLDrv and UTGLR renderers, OpenAL Soft, libxmp, gl4es and (Windows) SDL2 are fetched.

### Linux x86
1. Install git, cmake (>= 3.21), make, gcc and g++ with 32-bit (multilib) support, and a 32-bit SDL2 and OpenGL.
   * Debian/Ubuntu:
     ```
     sudo dpkg --add-architecture i386 && sudo apt update
     sudo apt install git cmake make gcc g++ gcc-multilib g++-multilib libsdl2-dev:i386 libgl-dev:i386
     ```
   * Arch/Manjaro: enable `[multilib]`, then `sudo pacman -S git cmake make gcc lib32-glibc lib32-gcc-libs lib32-mesa`.
     32-bit SDL2 is not in the official repos: install `lib32-sdl2-compat` from the AUR, or build SDL2 yourself (below).
2. Build:
   ```
   cmake --preset native && cmake --build --preset native -j$(nproc)
   ```
3. The result is `build-native/UnrealTournament/UnrealTournament` plus `libopenal.so.1`, which must stay next to it.
   A dedicated server, `build-native/UCC/ucc-bin`, is built too.

**64-bit (x86_64):** use the `native64` preset instead of `native`, with the regular 64-bit libraries in place of the i386/lib32 ones. The result lands in `build-native64/UnrealTournament/`.

**32-bit SDL2 from source** (when your distro has no package): build it into any prefix and point the preset at it.
```
git clone --depth 1 --branch release-2.32.10 https://github.com/libsdl-org/SDL.git
cmake -S SDL -B SDL/build -DCMAKE_C_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --build SDL/build -j$(nproc) && cmake --install SDL/build
cmake --preset native -DSDL2_INCLUDE_DIR=<prefix>/include/SDL2 "-DSDL2_LIBRARY=-L<prefix>/lib -lSDL2"
```
Copy `<prefix>/lib/libSDL2-2.0.so.0` next to the binary (or add `<prefix>/lib` to `LD_LIBRARY_PATH`).

### Windows x86 (cross-compiled from Linux)
1. Install the MinGW-w64 i686 toolchain: `g++-mingw-w64-i686-posix` (Debian/Ubuntu) or `mingw-w64-gcc` (Arch).
2. Build:
   ```
   cmake --preset windows && cmake --build --preset windows -j$(nproc)
   ```
3. The result is `build-windows/UnrealTournament/UnrealTournament.exe` plus `OpenAL32.dll`, which must stay next to it.
   A dedicated server, `build-windows/UCC/ucc.exe`, is built too.
4. For 64-bit Windows, install the x86_64 toolchain (`g++-mingw-w64-x86-64-posix`) and use the `windows64` preset; the
   result is in `build-windows64/`.

### Windows x86 (MSYS2 on Windows)
1. Install MSYS2 and open the `MINGW32` prompt. **Do not** use the `MINGW64` or `MSYS` prompts.
2. Install dependencies: `pacman -S git make mingw-w64-i686-toolchain mingw-w64-i686-cmake mingw-w64-i686-SDL2`
3. Build with the same `windows` preset as above. The output is the same, and `SDL2.dll` from `/mingw32/bin` must sit next to the exe.

### Web (WebAssembly)
1. Install [emsdk](https://github.com/emscripten-core/emsdk) 5.0.7 and load it: `./emsdk install 5.0.7 && ./emsdk activate 5.0.7 && source ./emsdk_env.sh`
2. Build:
   ```
   cmake --preset wasm-perf && cmake --build --preset wasm-perf -j$(nproc)
   ```
3. The result is `build-wasm-perf/SDLLaunch/` (`SDLLaunch.{js,wasm,data}` + `meta.json`). It is not playable on its own: the frontend repo serves it.

## Running

1. Install the retail UT99 game files.
2. Copy the files you built (see each platform above) to `UnrealTournament/System`. Overwrite everything.
3. Run `System/UnrealTournament` (Linux) or `System/UnrealTournament.exe` (Windows). The engine picks the SDL client and a working renderer itself.

Use real copies of the game files, not symlinks (under wine every package otherwise becomes a silent `FileNotFound`).
Headless check: `-NULLRENDERER` (Linux) or `-SOFTWARERENDERER` (Windows, also under wine); the log should reach `Init: Game engine initialized`.
On Linux, SIGTERM, SIGINT (Ctrl-C) or SIGHUP ends the game or a dedicated server cleanly; a second one is ignored.

## Credits

* Epic Games wrote Unreal Tournament and its v400 source, including its Linux port (the POSIX layer, the socket header and the launcher skeleton are Epic's own).
* maximqaxd ([maximqaxd/ut99dc](https://github.com/maximqaxd/ut99dc)) made the UT99 port this engine descends from: the GCC build, the SDL2 client and the Dreamcast drivers AICADrv, KOSDrv, PVRDrv and DCUtil. In it: the Dreamcast audio engine by Josh Pearson (with thanks to SKMP), Tri Stripper by Tanguy Fautré, stb_image_write by Sean Barrett, and KallistiOS.
* fgsfds ([fgsfdsfgs/UE1](https://github.com/fgsfdsfgs/UE1)) wrote the SDL2, OpenGL ES and OpenAL drivers ut99dc built on.
* OldUnreal wrote XOpenGLDrv (Smirftsch, Sebastian "Han" Kaufel, Stijn Volckaert, Marco888, Buggie, Higor, rX; the repository was started by Ryan C. Gordon), used here through the fork 10-p/XOpenGLDrv (BSD-3); the DXT decoder in it is by Benjamin Dobell and Matthäus "Anteru" Chajdas (MIT, via gl4es).
* Chris Dohnal wrote the UTGLR OpenGLDrv (fork 10-p/utglr-copy), on Daniel Vogel's (Loki) OpenGLDrv.
* ptitSeb wrote gl4es, on Ryan Hileman's glshim.
* The libraries: OpenAL Soft (Chris Robinson, kcat; LGPL), libxmp, SDL2, glad (David Herberth), glm (G-Truc Creation) and Emscripten.
* [alexstrout/foxWSFix-UT99](https://github.com/alexstrout/foxWSFix-UT99) is the reference for the weapon field-of-view behaviour; [Andiweli/UT99-Android](https://github.com/Andiweli/UT99-Android) gave SDL2 glue ideas.

## Note

Unreal Engine, Unreal Tournament and any related trademarks or copyrights are owned by Epic Games. This repository is not affiliated with or endorsed by Epic Games.
Do not use for commercial purposes.
