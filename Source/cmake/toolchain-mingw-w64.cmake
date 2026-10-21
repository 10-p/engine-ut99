# toolchain-mingw-w64.cmake — cross-compile a Windows binary from Linux with MinGW-w64.
#
# This is the compiler MSYS2 ships in its `mingw32` environment (`mingw-w64-i686-toolchain`), so a build from
# an MSYS2 prompt needs no toolchain file; this file supplies only what a cross build cannot infer.
#
# MINGW_TARGET_ARCH picks the compiler: i686 (the windows preset) or x86_64 (windows64, which also sets
# UE_ALLOW_64BIT; without it Source/CMakeLists.txt would demote the compiler to -m32).
#
# Usage: `cmake --preset windows` or `--preset windows64` (the presets point here). Standalone:
#   cmake -S Source -B build-windows -DCMAKE_TOOLCHAIN_FILE=Source/cmake/toolchain-mingw-w64.cmake

set(CMAKE_SYSTEM_NAME Windows)
set(MINGW_TARGET_ARCH "i686" CACHE STRING "MinGW-w64 target arch: i686 or x86_64")
set(CMAKE_SYSTEM_PROCESSOR ${MINGW_TARGET_ARCH})

set(_MINGW_TRIPLE "${MINGW_TARGET_ARCH}-w64-mingw32")

set(CMAKE_C_COMPILER   ${_MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${_MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER  ${_MINGW_TRIPLE}-windres)

# Look for headers and libraries in the cross sysroot, never the host's /usr.
set(CMAKE_FIND_ROOT_PATH /usr/${_MINGW_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
