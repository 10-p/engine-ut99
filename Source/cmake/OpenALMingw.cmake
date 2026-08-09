# OpenALMingw.cmake — OpenAL Soft for the MinGW-w64 (Windows) build.
#
# ufront step 2.34. Sibling of SDL2Mingw.cmake, same reasoning: every other platform gets OpenAL from
# somewhere that already exists (the system `-lopenal` on native Linux, Emscripten's built-in on the web),
# and a *cross* build from Linux has neither — so fetch OpenAL Soft's own official prebuilt Windows
# binaries. Nothing binary is vendored into git.
#
# ★ WHY A DLL AND NOT A STATIC LINK. Everything else in the Windows build is linked in, and the step's
# headline is "one file you can drop somewhere and run". OpenAL is the deliberate exception:
#   - The prebuilt drop is a DLL + import library; building openal-soft from source would pull a C++17
#     subproject into an engine that compiles at gnu++14 with its own global allocator overrides — the
#     exact combination that produced a null-ctor crash when this was tried on Android (see the
#     `android-nativelib-integration-openal` notes). Late-loading the shared library is the arrangement
#     that works there and on desktop Linux.
#   - Shipping a DLL beside the exe is explicitly acceptable for these development binaries (2.34 rule 5,
#     which allows ANGLE's libEGL/libGLESv2 for the same reason).
# The alternative was a SILENT Windows build (AudioDevice=SoundDrv.NullAudioSubsystem), and a null driver
# is not a usable default.
#
# Sets OPENAL_INCLUDE_DIR + OPENAL_LIBRARY, and defines OPENAL_MINGW_RUNTIME_DLL — the implementation DLL
# that has to sit next to the .exe. Callers copy it there (see Unreal/CMakeLists.txt).

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
# CACHE INTERNAL, not a plain variable: this file is included from whichever directory decides OpenAL
# (the top level in engine-unreal1, NOpenALDrv/ in engine-ut99), while the POST_BUILD copy lives in the
# LAUNCHER's directory. A normal set() there is invisible to a sibling subdirectory, and the failure is
# silent — the exe links, and the DLL simply is not next to it.
set(OPENAL_MINGW_RUNTIME_DLL "${_OPENAL_MINGW_ROOT}/bin/${_OPENAL_MINGW_ARCHDIR}/soft_oal.dll"
    CACHE INTERNAL "OpenAL Soft implementation DLL to ship beside the executable")
set(OPENAL_MINGW_RUNTIME_DLL_NAME "OpenAL32.dll"
    CACHE INTERNAL "Name the import library expects the OpenAL Soft DLL to have")
