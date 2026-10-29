cmake_minimum_required(VERSION 3.15)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

project(UT99 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)

if (NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
  if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
  endif()
endif()

# Options

# Web build mode (Emscripten only; the three modes are listed further down). UE_WEB_PERF is read by
# UnrealTournament/CMakeLists.txt for the link flags; UE_WEB_DEBUG selects -O0 over the -O2 dev default.
option(UE_WEB_PERF "Emscripten: shipping build — link at -O3 -g0, assertions off" OFF)
option(UE_WEB_DEBUG "Emscripten: -O0 instead of the -O2 dev default (nothing inlined, every local live)" OFF)
option(USE_SDL "Use the SDL2 client (NSDLDrv); off only for the Dreamcast client (KOSDrv)" ON)
option(LOW_MEMORY "Conserve memory as much as possible" OFF)
option(BUILD_UNREALTOURNAMENT "Build the launcher" ON)
# BUILD_EDITOR is a runtime dependency of a playable game: maps and meshes reference classes of the Editor
# package (without its natives a map fails with "Can't find Class Editor.TransBuffer" and falls back to the
# default map). This is the stripped, no-GUI Editor (NullEdHook) that links only Engine and Core.
option(BUILD_EDITOR "Build the Editor package (a RUNTIME dependency — see above)" ON)
option(BUILD_UTGLR "Build OpenGLDrv (UTGLR, fetched)" OFF)
option(BUILD_PVRDRV "Build PVRDrv (Dreamcast only)" OFF)
option(BUILD_NULLSOUNDDRV "Build SoundDrv (Null driver)" ON)
option(BUILD_NOPENALDRV "Build NOpenALDrv (OpenAL Soft sound + libxmp .umx music)" OFF)
option(UE_BUNDLED_OPENAL "Native Linux: build the openal-soft fork and ship libopenal.so.1 beside the binary instead of linking the system libopenal" ON)
option(BUILD_AICADRV "Build AICADrv (Dreamcast AICA sound driver)" OFF)
option(BUILD_DCUTIL "Build DCUtil" ON)
option(BUILD_STATIC "Link everything into a single binary" OFF)
# A 64-bit (LP64) build. OFF demotes an x86_64 compiler to -m32; ON builds x86_64 as x86_64. An arm64 compiler
# always builds 64-bit. The C++ side does not read this option: PLATFORM_64BITS comes from the compiler (Core.h).
option(UE_ALLOW_64BIT "Build x86_64 as a 64-bit (LP64) engine instead of demoting to -m32" OFF)

# Hardware render devices, one toggle each. The web build has all three (XOpenGLDrv, OpenGLDrv/UTGLR through
# gl4es, NOpenGLESDrv); natively the option defaults are NOpenGLESDrv only, and the presets turn on the rest.
if(EMSCRIPTEN)
  set(_UFRONT_DEF_NOPENGLES ON)
  set(_UFRONT_DEF_XOPENGL ON)
  set(_UFRONT_DEF_GL4ES ON)
else()
  set(_UFRONT_DEF_NOPENGLES ON)
  set(_UFRONT_DEF_XOPENGL OFF)
  set(_UFRONT_DEF_GL4ES OFF)
endif()
option(BUILD_NOPENGLESDRV "Build NOpenGLESDrv (GLES2 -> WebGL1)" ${_UFRONT_DEF_NOPENGLES})
option(BUILD_XOPENGLDRV "Build XOpenGLDrv (OldUnreal GL -> WebGL2/GLES3)" ${_UFRONT_DEF_XOPENGL})
option(BUILD_SOFTDRV "Build SoftDrv (CPU software renderer; -SOFTWARERENDERER). Pure software, no glad -> coexists with XOpenGL." ON)
option(BUILD_NULLDRV "Build NullDrv (GPU-free no-op render device; -NULLRENDERER). No GL context -> headless / dedicated-server run." ON)
option(USE_GL4ES "Build OpenGLDrv (UTGLR) against gl4es (desktop GL -> GLES2): the web and Android" ${_UFRONT_DEF_GL4ES})
# BUILD_UTGLR builds OpenGLDrv/UTGLR; USE_GL4ES (its GLES backend) implies it.
if(USE_GL4ES)
  set(BUILD_UTGLR ON)
endif()
# The XOpenGLDrv and OpenGLDrv/UTGLR sources are fetched (the fetch block above "# Subprojects" sets
# XOPENGL_SOURCE_DIR / UTGLR_SOURCE_DIR); only their build glue lives in-tree.


option(DREAMCAST_USE_FATFS "Try to load data from SD or IDE" OFF)
option(DREAMCAST_USE_FILE_POOL "Use file handle pool (wrap stdio)" ON)
option(DREAMCAST_NO_WHOLE_ARCHIVE "Use a hack to link only actually used code instead of using -whole-archive." OFF)
option(DREAMCAST_BUILD_CDI "Create CDI image after building (uses mkdcdisc)" OFF)

set(DREAMCAST_GAME_DATA "${CMAKE_SOURCE_DIR}/../UT99" CACHE PATH "Path to Unreal game data root (contains System, Maps, etc)")
set(DREAMCAST_IP_BIN "${CMAKE_SOURCE_DIR}/../UT99/IP.BIN" CACHE FILEPATH "Path to IP.BIN for Dreamcast disc")
set(DREAMCAST_CDI_OUTPUT "${CMAKE_SOURCE_DIR}/../UnrealTournament.cdi" CACHE FILEPATH "Output CDI image path")
set(DREAMCAST_IP_TXT "${CMAKE_SOURCE_DIR}/ip.txt" CACHE FILEPATH "Path to ip.txt template for makeip")
set(DREAMCAST_ELF_FILE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/UnrealTournament.elf" CACHE FILEPATH "Path to Unreal ELF used for mkdcdisc")

if(PLATFORM_DREAMCAST)
  set(BUILD_STATIC ON)
  set(USE_SDL OFF)
  set(LOW_MEMORY ON)
  set(BUILD_EDITOR OFF)
  set(BUILD_NOPENALDRV OFF)
  set(BUILD_NULLSOUNDDRV OFF)
  set(BUILD_AICADRV ON)
  set(BUILD_NOPENGLESDRV OFF)
  set(BUILD_GLDCDRV OFF)
  set(BUILD_PVRDRV ON)
  set(BUILD_DCUTIL OFF)
endif()

# Emscripten / WebAssembly: one static binary, no x86 asm, SDL2 and GLES2 from Emscripten's ports. The web
# configuration is forced here, so the wasm presets need no -D flags.
if(EMSCRIPTEN)
  set(BUILD_STATIC ON)            # single binary via UNREAL_STATIC + --whole-archive (like Dreamcast)
  set(USE_SDL ON)                 # NSDLDrv (SDL2 window/GL context -> WebGL); provided by -sUSE_SDL=2
  set(LOW_MEMORY OFF)
  set(BUILD_EDITOR ON)            # a runtime dependency (see the option)
  set(BUILD_DCUTIL OFF)
  set(BUILD_PVRDRV OFF)           # Dreamcast render
  set(BUILD_AICADRV OFF)          # Dreamcast audio
  # XOpenGLDrv and OpenGLDrv/UTGLR (through USE_GL4ES) come from the option defaults above. NOpenGLESDrv is
  # forced with set(), because option() only supplies a default and a cached OFF would win.
  set(BUILD_NOPENGLESDRV ON)
  set(BUILD_NULLSOUNDDRV ON)      # SoundDrv's silent audio subsystem
  set(BUILD_NOPENALDRV ON)        # OpenAL Soft + libxmp; the launcher selects it
  add_definitions(-DASM=0)        # Core/Render use their portable (non-x86) code paths
  # -pthread at compile time too: it defines __EMSCRIPTEN_PTHREADS__, which the threaded code paths are guarded
  # on (NSDLViewport's WebGL context creation, XOpenGL's entry-point resolver, appRequestExit), and gives every
  # object, the fetched trees included, the atomics a shared-memory link expects.
  add_compile_options(-pthread)
  # The SDL2 port at compile time too: <SDL2/SDL.h> exists in the Emscripten cache only once the port is
  # installed.
  add_compile_options(-sUSE_SDL=2)
  # UE1 assumes Win32 /Zp4 struct packing: a reflected C++ class's size must equal its script property size.
  # wasm32 aligns 8-byte members to 8, so pack every source to 4.
  add_compile_options(-fpack-struct=4 -Wno-address-of-packed-member)
  # clang rejects a non-POD FString passed through variadic debugf/appErrorf; GCC tolerates it.
  add_compile_options(-Wno-error=non-pod-varargs -Wno-non-pod-varargs)
  # Exceptions: the engine's objects use Emscripten's default (no catching). -fexceptions overflows V8's call
  # stack through invoke_ trampolines in UE1's deep package loader, and -fwasm-exceptions is incompatible with
  # ASYNCIFY. Recoverable load failures resolve to NULL instead; other errors abort. openal-soft reports AL errors
  # by throwing, so its objects alone are -fexceptions (cmake/OpenALSoft.cmake) and the link enables catching.
endif()


# Android: the playable client as one shared library, libUT99.so, loaded by the Gradle project's SDLActivity
# (Android/). USE_SDL OFF is the headless ucc-bin of the android-*-ucc presets, which this block leaves alone.
# SDL2 is fetched and built shared beside it (the Dependencies section), openal-soft and libxmp link in
# statically, and the render devices are XOpenGLDrv (GLES 3), OpenGLDrv/UTGLR through gl4es (GLES 2),
# NOpenGLESDrv (GLES 2), SoftDrv and NullDrv.
if(ANDROID AND USE_SDL)
  set(BUILD_STATIC ON)
  set(BUILD_EDITOR ON)            # a runtime dependency (see the option)
  set(BUILD_DCUTIL OFF)
  set(BUILD_PVRDRV OFF)
  set(BUILD_AICADRV OFF)
  set(BUILD_XOPENGLDRV ON)
  set(USE_GL4ES ON)
  set(BUILD_UTGLR ON)
  set(BUILD_NOPENGLESDRV ON)
  set(BUILD_SOFTDRV ON)
  set(BUILD_NULLDRV ON)
  set(BUILD_NULLSOUNDDRV ON)
  set(BUILD_NOPENALDRV ON)
  set(UE_ANDROID_CLIENT ON)
  # The app's own platform paths (the game folder from the bridge, the log on logcat); the headless
  # ucc-bin of the android-*-ucc presets keeps the plain POSIX ones.
  add_definitions(-DUE_ANDROID_APP=1)
  # The static packages become part of a shared library.
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
if(ANDROID)
  # clang rejects a non-POD FString passed through variadic debugf/appErrorf; GCC tolerates it.
  add_compile_options(-Wno-error=non-pod-varargs -Wno-non-pod-varargs)
  # UE1 calls methods through a null pointer and tests `this` inside them; clang deletes those tests.
  add_compile_options(-fno-delete-null-pointer-checks)
endif()

if(BUILD_STATIC)
  if(MSVC)
    message(FATAL_ERROR "Static mode is not supported with MSVC")
  endif()
  set(LIB_TYPE STATIC)
  add_definitions(-DUNREAL_STATIC)
  message(STATUS "Static mode enabled")
else()
  set(LIB_TYPE SHARED)
endif()

# Determine target arch and get normalized arch string

include(TargetArch)
target_architecture(TARGET_ARCH)

set(TARGET_IS_X86 FALSE)
set(TARGET_IS_ARM FALSE)
set(TARGET_IS_64BIT FALSE)
set(TARGET_IS_BIG_ENDIAN FALSE)

if(TARGET_ARCH MATCHES "arm")
  set(TARGET_IS_ARM TRUE)
  if(TARGET_ARCH MATCHES "64")
    # arm64/aarch64
    set(TARGET_IS_64BIT TRUE)
    set(TARGET_ARCH "arm64")
  else()
    # arm
    set(TARGET_ARCH "arm")
  endif()
elseif((TARGET_ARCH MATCHES "86") OR (TARGET_ARCH STREQUAL "ia64"))
  set(TARGET_IS_X86 TRUE)
  if(TARGET_ARCH MATCHES "64")
    # x86_64/amd64
    set(TARGET_IS_64BIT TRUE)
    set(TARGET_ARCH "x86_64")
  else()
    # x86
    set(TARGET_ARCH "x86")
  endif()
endif()

if(PLATFORM_DREAMCAST)
  set(TARGET_ARCH "sh4")
endif()

# UE1 needs a signed char (UnGnuG.h static_asserts it), and ARM's default is unsigned.
if(TARGET_IS_ARM)
  add_compile_options(-fsigned-char)
endif()

message(STATUS "Target arch: ${TARGET_ARCH}")
message(STATUS "64-bit: ${TARGET_IS_64BIT}")

if(TARGET_IS_64BIT)
  if(MSVC)
    # LLP64 (Windows x64) has never been built or run; refuse it.
    message(FATAL_ERROR "64-bit MSVC builds are not supported. Use -A Win32.")
  elseif(TARGET_IS_X86 AND NOT UE_ALLOW_64BIT AND NOT ANDROID)
    # (Android takes the width of its ABI: its x86_64 is a 64-bit engine like arm64.)
    message(STATUS "Building x86 binary with x86_64 compiler (pass -DUE_ALLOW_64BIT=ON for x86_64)")
    set(TARGET_IS_64BIT FALSE)
    set(TARGET_ARCH "x86")
    add_compile_options(-m32)
  else()
    message(STATUS "Building a 64-bit (LP64) engine for ${TARGET_ARCH}")
  endif()
endif()

# Determine target OS/platform

set(TARGET_IS_WINDOWS FALSE)
set(TARGET_IS_UNIX FALSE)
set(TARGET_IS_DREAMCAST FALSE)
if(WIN32)
  set(TARGET_IS_WINDOWS TRUE)
else()
  set(TARGET_IS_UNIX TRUE)
endif()

if(PLATFORM_DREAMCAST)
  set(TARGET_IS_DREAMCAST TRUE)
endif()

message(STATUS "Windows target: ${TARGET_IS_WINDOWS}")

# Common compiler options

if(BUILD_FOR_PSVITA AND NOT TARGET_IS_ARM)
  message(FATAL_ERROR "PSVita is ARM.")
endif()

if(TARGET_IS_X86)
  add_definitions(-DPLATFORM_X86)
elseif(TARGET_IS_DREAMCAST)
  set(DREAMCAST_CFLAGS
    -m4-single -fsigned-char -fno-short-enums -fexceptions -fno-rtti -fsingle-precision-constant -flto=8
    -fomit-frame-pointer -fstack-usage
  )
  set(DREAMCAST_WFLAGS
    -Wno-misleading-indentation -Wno-reorder -Wno-unused-variable -Wno-unused-value -Wno-overloaded-virtual -Wno-sign-compare
    -Wno-address -Wno-nonnull-compare -Wno-unused-but-set-variable -Wno-mismatched-new-delete -Wno-int-in-bool-context -Wno-comment
  )
  add_compile_options(${DREAMCAST_CFLAGS} ${DREAMCAST_WFLAGS})
  add_link_options(${DREAMCAST_CFLAGS} ${DREAMCAST_WFLAGS})
  add_definitions(-DPLATFORM_DREAMCAST)
  if(DREAMCAST_USE_FATFS)
    add_definitions(-DDREAMCAST_USE_FATFS)
  endif()
  if(DREAMCAST_USE_FILE_POOL)
    add_definitions(-DDREAMCAST_USE_FILE_POOL)
    add_link_options(-Wl,--wrap=fopen -Wl,--wrap=fclose -Wl,--wrap=fread -Wl,--wrap=fwrite -Wl,--wrap=fseek -Wl,--wrap=ftell -Wl,--wrap=setvbuf)
  endif()
  include_directories(
    ${KOS_BASE}/include
    ${KOS_BASE}/kernel/arch/dreamcast/include
    ${KOS_BASE}/addons/include
    ${KOS_BASE}/../kos-ports/include
  )
  link_directories(
    ${KOS_BASE}/lib/dreamcast
    ${KOS_BASE}/addons/lib/dreamcast
    ${KOS_BASE}/../kos-ports/lib
  )
endif()

if(TARGET_IS_BIG_ENDIAN)
  add_definitions(-DPLATFORM_BIG_ENDIAN)
else()
  add_definitions(-DPLATFORM_LITTLE_ENDIAN)
endif()

if(TARGET_IS_WINDOWS)
  add_definitions(-DPLATFORM_WIN32)
  add_definitions(-DWIN32)
  add_definitions(-DWINDOWS_IGNORE_PACKING_MISMATCH)
  if(MINGW)
    # Link GCC's runtime statically, so the .exe imports only system DLLs (libgcc_s and libstdc++ live in the
    # toolchain, not in a Windows install).
    add_link_options(-static-libgcc -static-libstdc++)
    # A posix-thread-model MinGW (MSYS2, Arch/Fedora packages) also pulls in libwinpthread-1.dll through
    # libstdc++; link it statically too, or the exe does not start outside the build machine.
    add_link_options(-Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
    # UE1 assumes Win32 /Zp4 struct packing: a reflected C++ class's size must equal its script property size.
    # MinGW follows the MSVC ABI and aligns double and int64 to 8 (x86-32 Linux aligns them to 4), so every source
    # is packed to 4, as MSVC's /Zp4 does. Not on x86_64, where it would repack the Win64 API's structs too: there
    # the script-mirrored headers pack themselves (#pragma pack(push,4)).
    if(NOT TARGET_IS_64BIT)
      add_compile_options(-fpack-struct=4 -Wno-address-of-packed-member)
    endif()
    # Windows puts in libraries what glibc keeps in libc: sockets (Core's appComputerName/appGetLocalIP
    # and all of IpDrv) are ws2_32, and appGetGUID's CoCreateGuid is ole32. Declared here, before the
    # add_subdirectory calls, so every package inherits them.
    link_libraries(ws2_32 ole32)
  endif()
else()
  add_definitions(-DPLATFORM_POSIX)
  if(NOT TARGET_IS_DREAMCAST)
    add_definitions(-DPLATFORM_CASE_SENSITIVE_FS)
  endif()
  if(TARGET_IS_DREAMCAST)
    set(CMAKE_EXECUTABLE_SUFFIX ".elf")
  endif()
endif()

if(USE_SDL)
  add_definitions(-DPLATFORM_SDL)
endif()

if(LOW_MEMORY)
  add_definitions(-DPLATFORM_LOW_MEMORY)
endif()

if(MSVC)
  message(STATUS "Building ${CMAKE_BUILD_TYPE} with MSVC")
  add_definitions(-DPLATFORM_MSVC)
  add_compile_options(/nologo /fp:precise /analyze- /MP /MD /Gd /Gy /GS /Gm- /EHsc /Zi /Zc:wchar_t /Zc:inline /Zp4)
  add_compile_options(/wd4291 /wd4297)
  if(TARGET_IS_64BIT)
    set(MSVC_LIBDIR "Win64")
  else()
    set(MSVC_LIBDIR "Win32")
  endif()
else()
  message(STATUS "Building ${CMAKE_BUILD_TYPE} with GCC or Clang")
  if(EMSCRIPTEN)
    # The three web build modes:
    #
    #   preset        UE_WEB_PERF  UE_WEB_DEBUG  compile   what it is for
    #   wasm-perf     ON           -             -O2 (*)   shipping: link-time -O3 -g0, no assertions
    #   wasm          OFF          OFF           -O2 -g    the dev default: named stacks, DWARF, assertions
    #   wasm-debug    OFF          ON            -O0 -g    last resort: every local live, nothing inlined
    #
    # (*) The perf build differs from the dev build at link only (UnrealTournament/CMakeLists.txt).
    #
    # -O0 is for when the optimiser is the suspect: a bug that shows at one -O level and not another is almost
    # always undefined behaviour.
    if(UE_WEB_DEBUG)
      add_compile_options(-O0 -g)
    elseif(UE_WEB_PERF)
      # The shipping build carries no compile-side DWARF: -O2 at compile, -O3 -g0 at link.
      add_compile_options(-O2)
    else()
      add_compile_options(-O2 -g)
    endif()
  # CMake puts CMAKE_<LANG>_FLAGS_<CONFIG> ahead of add_compile_options() and the last -O wins; the level
  # appended here matches the injected one. Core/CMakeLists.txt lowers UnCorSc.cpp and UnProp.cpp to -Og on
  # purpose (gcc miscompiles FFrame::Step() above it). The per-config variables also carry -DNDEBUG, so they
  # are not blanked.
  elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Og -g)
    add_link_options(-g)
  elseif(LOW_MEMORY)
    add_compile_options(-Os)
  else()
    add_compile_options(-O2)
  endif()
  add_compile_options(-ffunction-sections -fno-strict-aliasing)
  add_compile_options(-Wno-write-strings -Wno-terminate)
  # GCC: keep the stores a destructor makes to its own object. UE1 destroys native config arrays and strings
  # twice (UObject::ExitProperties, then the C++ destructor), which is harmless only because ~FArray leaves
  # `Data = NULL; ArrayNum = ArrayMax = 0` behind; -flifetime-dse would delete those stores.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fno-lifetime-dse)
  endif()
endif()

# Dependencies

if(USE_SDL)
  if(MSVC)
    if(NOT DEFINED SDL2_INCLUDE_DIR)
      set(SDL2_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../Thirdparty/SDL2/include")
    endif()
    if(NOT DEFINED SDL2_LIBRARY)
      set(SDL2_LIBRARY "${CMAKE_SOURCE_DIR}/../Thirdparty/SDL2/lib/${MSVC_LIBDIR}/SDL2.lib")
    endif()
  else()
    # Won't bother with find_package as we're probably cross compiling.
    if(NOT DEFINED SDL2_INCLUDE_DIR)
      set(SDL2_INCLUDE_DIR )
    endif()
    if(NOT DEFINED SDL2_LIBRARY)
      if(EMSCRIPTEN)
        # The -sUSE_SDL=2 port supplies SDL2 and picks the threaded variant under -pthread; an explicit -lSDL2
        # would resolve to the non-threaded archive.
        set(SDL2_LIBRARY )
      elseif(ANDROID)
        # SDL2 from its source, built shared (libSDL2.so: SDLActivity loads it before the game library and its
        # JNI lives in it). The Gradle project passes FETCHCONTENT_SOURCE_DIR_SDL2, the checkout whose Java it
        # compiles, so the C and Java sides are one version. The target carries its include directories.
        include(FetchContent)
        set(SDL2_GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git" CACHE STRING "SDL2 git repo")
        set(SDL2_GIT_TAG "release-2.32.x" CACHE STRING "SDL2 git tag/branch/commit")
        message(STATUS "SDL2: fetching ${SDL2_GIT_REPOSITORY}@${SDL2_GIT_TAG}")
        FetchContent_Declare(SDL2 GIT_REPOSITORY "${SDL2_GIT_REPOSITORY}" GIT_TAG "${SDL2_GIT_TAG}" GIT_SHALLOW TRUE)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL2)
        set(SDL2_LIBRARY SDL2)
      elseif(MINGW AND CMAKE_CROSSCOMPILING)
        # Cross-building for Windows: fetch SDL2's official prebuilt MinGW binaries. Under MSYS2 (not
        # cross-compiling) pacman's mingw-w64-i686-SDL2 answers -lSDL2.
        include(SDL2Mingw)
      else()
        set(SDL2_LIBRARY "-lSDL2")
      endif()
    endif()
  endif()
endif()


# The XOpenGLDrv and OpenGLDrv/UTGLR sources are fetched from their forks at configure time; the in-tree build
# glue (Source/XOpenGLDrv, Source/OpenGLDrv) compiles them from XOPENGL_SOURCE_DIR / UTGLR_SOURCE_DIR. Neither
# repo has a root CMakeLists.txt, so FetchContent_MakeAvailable only populates. To build a local checkout:
#     cmake --preset native -DFETCHCONTENT_SOURCE_DIR_XOPENGLDRV=<path> -DFETCHCONTENT_SOURCE_DIR_UTGLR=<path>
# GIT_TAG defaults to a branch; pin *_GIT_TAG to a commit for a reproducible build.
if(NOT PLATFORM_DREAMCAST AND BUILD_XOPENGLDRV)
  set(XOPENGL_GIT_REPOSITORY "https://github.com/10-p/XOpenGLDrv.git" CACHE STRING "XOpenGLDrv git repo (OldUnreal fork)")
  set(XOPENGL_GIT_TAG "ufront" CACHE STRING "XOpenGLDrv git tag/branch/commit")
  include(FetchContent)
  message(STATUS "XOpenGLDrv: fetching ${XOPENGL_GIT_REPOSITORY}@${XOPENGL_GIT_TAG}")
  FetchContent_Declare(XOpenGLDrv
    GIT_REPOSITORY "${XOPENGL_GIT_REPOSITORY}" GIT_TAG "${XOPENGL_GIT_TAG}" GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(XOpenGLDrv)
  set(XOPENGL_SOURCE_DIR "${xopengldrv_SOURCE_DIR}")
  message(STATUS "XOpenGLDrv: source = ${XOPENGL_SOURCE_DIR}")
endif()

if(NOT PLATFORM_DREAMCAST AND BUILD_UTGLR)
  set(UTGLR_GIT_REPOSITORY "https://github.com/10-p/utglr-copy.git" CACHE STRING "OpenGLDrv/UTGLR git repo (Dohnal drop + our port)")
  set(UTGLR_GIT_TAG "main" CACHE STRING "OpenGLDrv/UTGLR git tag/branch/commit")
  include(FetchContent)
  message(STATUS "OpenGLDrv/UTGLR: fetching ${UTGLR_GIT_REPOSITORY}@${UTGLR_GIT_TAG}")
  FetchContent_Declare(utglr
    GIT_REPOSITORY "${UTGLR_GIT_REPOSITORY}" GIT_TAG "${UTGLR_GIT_TAG}" GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(utglr)
  set(UTGLR_SOURCE_DIR "${utglr_SOURCE_DIR}")
  message(STATUS "OpenGLDrv/UTGLR: source = ${UTGLR_SOURCE_DIR}")
endif()

# Subprojects

# UWeb's natives are built because the game ships UWeb.u, and binding it without them is a fatal error.
set(INSTALL_TARGETS Core Engine Render IpDrv UWeb Fire)

set(CMAKE_SHARED_LIBRARY_PREFIX "")

add_subdirectory(Core)
#add_subdirectory(Audio)
add_subdirectory(Engine)
add_subdirectory(Render)
add_subdirectory(IpDrv)
add_subdirectory(UWeb)
add_subdirectory(Fire)
# ucc (dedicated server, make, commandlets): native only; the Android app has the android-*-ucc presets.
if(NOT EMSCRIPTEN AND NOT UE_ANDROID_CLIENT)
  add_subdirectory(UCC)
endif()

# The WebRTC net driver: a web-only package over the glue ABI (vendor/glue), selected by the launcher.
if(EMSCRIPTEN)
  add_subdirectory(WebRTCDrv)
  list(APPEND INSTALL_TARGETS WebRTCDrv)
endif()

if(USE_SDL)
  # SDL2
  add_subdirectory(NSDLDrv)
  list(APPEND INSTALL_TARGETS NSDLDrv)
elseif(TARGET_IS_DREAMCAST)
  # native Dreamcast driver
  add_subdirectory(KOSDrv)
  list(APPEND INSTALL_TARGETS KOSDrv)
endif()

# Static builds link every driver under --whole-archive. NOpenGLESDrv and XOpenGLDrv each carry a glad: on
# the web NOpenGLESDrv compiles none (Emscripten links the GLES2 entry points), and natively Thirdparty/glad_es
# gives its externals a private prefix (glad_symbol_prefix.h), so the two co-link.

if(BUILD_UTGLR)
  # gl4es (desktop GL -> GLES2) for OpenGLDrv/UTGLR on a GLES context (the web, Android), fetched when USE_GL4ES
  # or on Emscripten; GL4ES_SOURCE_DIR names a local checkout. See Source/OpenGLDrv/PORTING.md.
  if(USE_GL4ES OR EMSCRIPTEN)
    set(GL4ES_SOURCE_DIR "" CACHE PATH "Local gl4es checkout (vanilla or fork). Empty => fetch from git.")
    set(GL4ES_GIT_REPOSITORY "https://github.com/10-p/gl4es.git" CACHE STRING "gl4es git repo (the 10-p fork)")
    set(GL4ES_GIT_TAG "ufront" CACHE STRING "gl4es git tag/branch/commit to fetch")
    include(FetchContent)
    if(GL4ES_SOURCE_DIR)
      message(STATUS "gl4es: using local source ${GL4ES_SOURCE_DIR}")
      FetchContent_Declare(GL4ES SOURCE_DIR "${GL4ES_SOURCE_DIR}")
    else()
      message(STATUS "gl4es: fetching ${GL4ES_GIT_REPOSITORY}@${GL4ES_GIT_TAG}")
      FetchContent_Declare(GL4ES GIT_REPOSITORY "${GL4ES_GIT_REPOSITORY}" GIT_TAG "${GL4ES_GIT_TAG}" GIT_SHALLOW TRUE)
    endif()
    # Static lib, GLES2 target, no X11/EGL (NSDLDrv owns the context), explicit init.
    set(NOX11 ON CACHE BOOL "" FORCE)
    set(NOEGL ON CACHE BOOL "" FORCE)
    set(STATICLIB ON CACHE BOOL "" FORCE)
    set(DEFAULT_ES 2 CACHE STRING "" FORCE)
    set(NO_INIT_CONSTRUCTOR ON CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.10)  # gl4es' old cmake_minimum_required vs CMake 4.x
    FetchContent_MakeAvailable(GL4ES)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    if(NOT TARGET GL)
      message(FATAL_ERROR "gl4es: expected static target 'GL' was not created.")
    endif()
    # gl4es hardcodes its archive output; redirect per build dir so native/wasm don't collide.
    set_target_properties(GL PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    if(NOT TARGET gl4es::GL)
      add_library(gl4es::GL ALIAS GL)  # namespaced handle; never silently falls back to system -lGL
    endif()
  endif()

  # Build glue lives in-tree (Source/OpenGLDrv); it compiles/includes from ${UTGLR_SOURCE_DIR} (fetched).
  add_subdirectory(OpenGLDrv)
  list(APPEND INSTALL_TARGETS OpenGLDrv)
endif()

if(NOT PLATFORM_DREAMCAST AND BUILD_NOPENGLESDRV)
  add_subdirectory(NOpenGLESDrv)
  list(APPEND INSTALL_TARGETS NOpenGLESDrv)
endif()

# XOpenGLDrv (OldUnreal; WebGL 2 / GLES3 on the web). Its v400 adaptations are gated `#if ENGINE_VERSION==400`
# in the fork; see Source/XOpenGLDrv/ENGINE_CHANGES.md.
if(NOT PLATFORM_DREAMCAST AND BUILD_XOPENGLDRV)
  # Build glue lives in-tree (Source/XOpenGLDrv); it compiles/includes from ${XOPENGL_SOURCE_DIR} (fetched).
  add_subdirectory(XOpenGLDrv)
  list(APPEND INSTALL_TARGETS XOpenGLDrv)
endif()

# SoftDrv: Epic's v400 CPU rasterizer, selected with -SOFTWARERENDERER.
if(NOT PLATFORM_DREAMCAST AND BUILD_SOFTDRV)
  add_subdirectory(SoftDrv)
  list(APPEND INSTALL_TARGETS SoftDrv)
endif()

# NullDrv: a render device that draws nothing and needs no GL context, selected with -NULLRENDERER.
if(NOT PLATFORM_DREAMCAST AND BUILD_NULLDRV)
  add_subdirectory(NullDrv)
  list(APPEND INSTALL_TARGETS NullDrv)
endif()

if(BUILD_PVRDRV)
  add_subdirectory(PVRDrv)
  list(APPEND INSTALL_TARGETS PVRDrv)
endif()

if(BUILD_NULLSOUNDDRV)
  add_subdirectory(SoundDrv)
  list(APPEND INSTALL_TARGETS SoundDrv)
endif()

# NOpenALDrv: OpenAL Soft sound and libxmp .umx music (fetched; pumped from Update(), no music thread).
if(BUILD_NOPENALDRV)
  if(EMSCRIPTEN)
    # The openal-soft fork (HRTF and EFX reverb on the web). EAX OFF: NOpenALDrv's reverb is EFX's
    # AL_EFFECT_EAXREVERB and it never calls the EAX API.
    include(OpenALSoft)
    ufront_openal_soft(PLATFORM WEB EAX OFF)
  elseif(ANDROID)
    # Android links the same fork statically into the game library, playing through OpenSL ES.
    include(OpenALSoft)
    ufront_openal_soft(PLATFORM ANDROID EAX OFF)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND UE_BUNDLED_OPENAL)
    # Native Linux builds the same fork as a shared library shipped beside the binary (cmake/OpenALSoft.cmake).
    include(OpenALSoft)
    ufront_openal_soft(PLATFORM NATIVE EAX OFF)
  endif()
  add_subdirectory(NOpenALDrv)
  list(APPEND INSTALL_TARGETS NOpenALDrv)
endif()

if(BUILD_AICADRV)
  add_subdirectory(AICADrv)
  list(APPEND INSTALL_TARGETS AICADrv)
endif()

if(BUILD_EDITOR)
  # GUI requires WinDrv
  add_subdirectory(Editor)
  list(APPEND INSTALL_TARGETS Editor)
endif()


if(BUILD_UNREALTOURNAMENT)
  add_subdirectory(UnrealTournament)
  list(APPEND INSTALL_TARGETS UnrealTournament)
endif()

# The Android bridge: a library of its own that GameActivity loads first to set the game folder, HOME and the
# working directory before SDLActivity starts SDL_main (Android/app/src/main/cpp/android_bridge.cpp).
if(UE_ANDROID_CLIENT)
  add_library(ut99_android_bridge SHARED "${CMAKE_SOURCE_DIR}/../Android/app/src/main/cpp/android_bridge.cpp")
  set_target_properties(ut99_android_bridge PROPERTIES PREFIX "lib")
  target_link_libraries(ut99_android_bridge log)
endif()

# assemble all modules and pdbs in one dir

if(BUILD_DCUTIL)
  set(BUILD_EDITOR ON)
  list(APPEND INSTALL_TARGETS DCUtil)
  add_subdirectory(DCUtil)
endif()

set(INSTALL_FILES )
set(INSTALL_PROGRAMS )

foreach(TGT ${INSTALL_TARGETS})
  get_target_property(TGT_TYPE ${TGT} TYPE)
  if(TGT_TYPE STREQUAL "EXECUTABLE")
    list(APPEND INSTALL_PROGRAMS "$<TARGET_FILE:${TGT}>")
  else()
    list(APPEND INSTALL_FILES "$<TARGET_FILE:${TGT}>")
  endif()
  if(MSVC)
    list(APPEND INSTALL_FILES "$<TARGET_PDB_FILE:${TGT}>")
  endif()
endforeach()

# Install executables with executable permissions preserved
if(INSTALL_PROGRAMS)
  install(PROGRAMS ${INSTALL_PROGRAMS} DESTINATION "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
endif()

# Install libraries and other files
if(INSTALL_FILES)
  install(FILES ${INSTALL_FILES} DESTINATION "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
endif()

# Dreamcast CDI image generation
if(TARGET_IS_DREAMCAST AND DREAMCAST_BUILD_CDI)
  find_program(MKDCDISC mkdcdisc)
  if(NOT MKDCDISC)
    message(FATAL_ERROR "mkdcdisc not found in PATH but DREAMCAST_BUILD_CDI=ON")
  endif()
  if(NOT DREAMCAST_GAME_DATA OR NOT EXISTS "${DREAMCAST_GAME_DATA}")
    message(FATAL_ERROR "DREAMCAST_GAME_DATA must point to the Unreal data directory when DREAMCAST_BUILD_CDI=ON")
  endif()
  if(NOT DREAMCAST_IP_TXT OR NOT EXISTS "${DREAMCAST_IP_TXT}")
    message(FATAL_ERROR "DREAMCAST_IP_TXT must point to ip.txt when DREAMCAST_BUILD_CDI=ON")
  endif()
  if(NOT DREAMCAST_IP_BIN)
    message(FATAL_ERROR "DREAMCAST_IP_BIN must not be empty when DREAMCAST_BUILD_CDI=ON")
  endif()

  if(NOT KOS_BASE)
    message(FATAL_ERROR "KOS_BASE is not set; required to locate makeip")
  endif()
  set(MAKEIP_EXE "${KOS_BASE}/utils/makeip/makeip")
  if(NOT EXISTS "${MAKEIP_EXE}")
    message(FATAL_ERROR "makeip not found at ${MAKEIP_EXE}")
  endif()

  get_filename_component(DREAMCAST_IP_BIN_DIR "${DREAMCAST_IP_BIN}" DIRECTORY)

  add_custom_command(OUTPUT "${DREAMCAST_IP_BIN}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DREAMCAST_IP_BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${DREAMCAST_IP_BIN}"
    COMMAND ${MAKEIP_EXE} "${DREAMCAST_IP_TXT}" "${DREAMCAST_IP_BIN}"
    DEPENDS "${DREAMCAST_IP_TXT}"
    COMMENT "Generating IP.BIN..."
    VERBATIM
  )

  add_custom_target(ip_bin ALL
    DEPENDS "${DREAMCAST_IP_BIN}"
  )

  add_custom_target(cdi
    COMMAND ${CMAKE_COMMAND} -E echo "Creating CDI image..."
    COMMAND ${MKDCDISC} -e "${DREAMCAST_ELF_FILE}" -D "${DREAMCAST_GAME_DATA}" -p "${DREAMCAST_IP_BIN}" -N -o "${DREAMCAST_CDI_OUTPUT}"
    DEPENDS UnrealTournament ip_bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    VERBATIM
  )
endif()