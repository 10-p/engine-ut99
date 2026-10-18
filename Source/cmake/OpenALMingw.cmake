# OpenALMingw.cmake — OpenAL Soft for the MinGW-w64 (Windows) build.
#
# Fetches OpenAL Soft's official prebuilt Windows binaries (nothing binary is vendored). The Windows build
# links everything else in; OpenAL is a DLL beside the exe, which keeps openal-soft's C++ and its allocator
# out of the engine's static link.
#
# Sets OPENAL_INCLUDE_DIR + OPENAL_LIBRARY, and defines OPENAL_MINGW_RUNTIME_DLL — the implementation DLL
# that has to sit next to the .exe. UnrealTournament/CMakeLists.txt copies it there.

set(OPENAL_MINGW_VERSION "1.25.2" CACHE STRING "OpenAL Soft release to fetch for the MinGW build")
set(OPENAL_MINGW_SHA256  "67a0c4b800bd860c93c04f38caf8cbe4875f9c84700ac430efc451f70e265434"
    CACHE STRING "SHA256 of openal-soft-<version>-bin.zip")

if(MINGW_TARGET_ARCH STREQUAL "x86_64")
  set(_OPENAL_MINGW_ARCHDIR "Win64")
else()
  set(_OPENAL_MINGW_ARCHDIR "Win32")
endif()

include(FetchContent)
FetchContent_Declare(OpenALMingw
  URL "https://github.com/kcat/openal-soft/releases/download/${OPENAL_MINGW_VERSION}/openal-soft-${OPENAL_MINGW_VERSION}-bin.zip"
  URL_HASH "SHA256=${OPENAL_MINGW_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
message(STATUS "OpenAL Soft (MinGW): fetching prebuilt ${OPENAL_MINGW_VERSION} for ${_OPENAL_MINGW_ARCHDIR}")
FetchContent_MakeAvailable(OpenALMingw)

set(_OPENAL_MINGW_ROOT "${openalmingw_SOURCE_DIR}")
if(NOT EXISTS "${_OPENAL_MINGW_ROOT}/libs/${_OPENAL_MINGW_ARCHDIR}/libOpenAL32.dll.a")
  message(FATAL_ERROR "OpenAL Soft (MinGW): libs/${_OPENAL_MINGW_ARCHDIR}/libOpenAL32.dll.a is missing — the zip layout changed.")
endif()

set(OPENAL_INCLUDE_DIR "${_OPENAL_MINGW_ROOT}/include")
set(OPENAL_LIBRARY     "${_OPENAL_MINGW_ROOT}/libs/${_OPENAL_MINGW_ARCHDIR}/libOpenAL32.dll.a")

# `soft_oal.dll` IS the implementation; `router/` holds the pass-through OpenAL32.dll that dispatches to a
# system-installed driver. We want the implementation, under the name the import library asks for.
# CACHE INTERNAL, not a plain variable: this file is included from NOpenALDrv/, while the POST_BUILD copy
# lives in the launcher's directory, a sibling that would not see a normal set().
set(OPENAL_MINGW_RUNTIME_DLL "${_OPENAL_MINGW_ROOT}/bin/${_OPENAL_MINGW_ARCHDIR}/soft_oal.dll"
    CACHE INTERNAL "OpenAL Soft implementation DLL to ship beside the executable")
set(OPENAL_MINGW_RUNTIME_DLL_NAME "OpenAL32.dll"
    CACHE INTERNAL "Name the import library expects the OpenAL Soft DLL to have")
