# SDL2Mingw.cmake — SDL2 for the MinGW-w64 (Windows) build.
#
# A cross build from Linux has no SDL2 to link against (native Linux and MSYS2 use the system's, the web the
# -sUSE_SDL=2 port, MSVC Thirdparty/), so this fetches SDL2's official prebuilt MinGW development binaries.
#
# Sets SDL2_INCLUDE_DIR and SDL2_LIBRARY (a full link line: static SDL2 + the Win32 libraries it needs,
# taken from the tarball's own sdl2.pc `Libs.private`).

set(SDL2_MINGW_VERSION "2.32.10" CACHE STRING "SDL2 release to fetch for the MinGW build")
set(SDL2_MINGW_SHA256  "83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2"
    CACHE STRING "SHA256 of SDL2-devel-<version>-mingw.tar.gz")

# The toolchain file sets _MINGW_TRIPLE; derive it from the compiler when this is included some other way.
if(NOT _MINGW_TRIPLE)
  get_filename_component(_MINGW_TRIPLE "${CMAKE_CXX_COMPILER}" NAME)
  string(REGEX REPLACE "-g\\+\\+.*$" "" _MINGW_TRIPLE "${_MINGW_TRIPLE}")
endif()

include(FetchContent)
FetchContent_Declare(SDL2Mingw
  URL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_MINGW_VERSION}/SDL2-devel-${SDL2_MINGW_VERSION}-mingw.tar.gz"
  URL_HASH "SHA256=${SDL2_MINGW_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
message(STATUS "SDL2 (MinGW): fetching prebuilt ${SDL2_MINGW_VERSION} for ${_MINGW_TRIPLE}")
FetchContent_MakeAvailable(SDL2Mingw)

set(_SDL2_MINGW_ROOT "${sdl2mingw_SOURCE_DIR}/${_MINGW_TRIPLE}")
if(NOT EXISTS "${_SDL2_MINGW_ROOT}/lib/libSDL2.a")
  message(FATAL_ERROR "SDL2 (MinGW): ${_SDL2_MINGW_ROOT}/lib/libSDL2.a is missing — the tarball layout changed.")
endif()

set(SDL2_INCLUDE_DIR "${_SDL2_MINGW_ROOT}/include")

# Order matters: -lmingw32 and SDL2main precede SDL2. SDL.h renames the engine's `int main()` to `SDL_main`,
# and SDL2main supplies the WinMain that calls it.
set(SDL2_LIBRARY
  -lmingw32
  "${_SDL2_MINGW_ROOT}/lib/libSDL2main.a"
  "${_SDL2_MINGW_ROOT}/lib/libSDL2.a"
  # sdl2.pc Libs.private — what static SDL2 pulls from Win32 itself.
  -ldinput8 -ldxguid -lsetupapi -lversion -luuid -limm32 -lole32 -loleaut32
  -lshell32 -luser32 -lgdi32 -lwinmm -lm
)
