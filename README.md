## What?

Unreal Tournament (1999) Unreal Engine 1 v400 source with modifications to make it run on modern systems and in the browser.
Requires assets from a retail UT99 install (v400 packages). For v436 assets, build the `feat/v436-compat` branch the same way.
Based on [maximqaxd/ut99dc](https://github.com/maximqaxd/ut99dc). Design notes and web-port details: [BUILD.md](BUILD.md).

## Changes from original source

* SDL2 windowing/client driver (NSDLDrv) and a static single-binary build (no `.so`/`.dll` packages).
* GLES2 (NOpenGLESDrv), OldUnreal XOpenGLDrv and UTGLR OpenGLDrv renderers, plus SoftDrv and NullDrv.
* OpenAL + libxmp audio driver (NOpenALDrv).
* GCC/MinGW support, including the Win32 struct-layout fixes.
* A WebAssembly build and a WebRTC net driver (WebRTCDrv) for the browser.
* Editor UI is not supported.

## Building

All builds are 32-bit and use the CMake presets in `Source/`, so run every `cmake` command from `Source/`.
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

## Note

Unreal Engine, Unreal Tournament and any related trademarks or copyrights are owned by Epic Games. This repository is not affiliated with or endorsed by Epic Games.
Do not use for commercial purposes.
