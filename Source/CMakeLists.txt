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

option(USE_SDL "Use NSDLDrv instead of WinDrv" ON)
option(LOW_MEMORY "Conserve memory as much as possible" OFF)
option(BUILD_UNREALTOURNAMENT "Build the launcher" ON)
option(BUILD_EDITOR "Build Editor (GUI support requires Windows)" ON)
option(BUILD_NOPENGLDRV "Build NOpenGLDrv" OFF)
option(BUILD_PVRDRV "Build PVRDrv (Dreamcast only)" OFF)
option(BUILD_NULLSOUNDDRV "Build SoundDrv (Null driver)" ON)
option(BUILD_NOPENALDRV "Build NOpenALDrv (OpenAL SFX + libxmp .umx music; web + native Linux)" OFF)
option(BUILD_AICADRV "Build AICADrv (Dreamcast AICA sound driver)" OFF)
option(BUILD_DCUTIL "Build DCUtil" ON)
option(BUILD_STATIC "Link everything into a single binary" OFF)

# ufront 2.9 — HW render devices: orthogonal per-renderer toggles + configurable source folders.
# NOpenGLESDrv (GLES2/WebGL1) is the historical device; XOpenGLDrv (OldUnreal, WebGL2/GLES3) and
# OpenGLDrv/UTGLR (via gl4es, WebGL1) are the 2.9 additions. Defaults are platform-aware: web ships
# XOpenGL + UTGLR and drops NOpenGLES (NOpenGLES + XOpenGLDrv both vendor a `glad` -> duplicate globals,
# so they cannot co-link under --whole-archive); native (.so) defaults to NOpenGLES only. Any combo is
# selectable via -D (subject to the FATAL_ERROR guard near the driver subdirs). See XOpenGLDrv/ENGINE_CHANGES.md.
if(EMSCRIPTEN)
  set(_UFRONT_DEF_NOPENGLES OFF)
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
option(USE_GL4ES "Build OpenGLDrv (UTGLR) against gl4es (desktop GL -> GLES2) for the WebGL path" ${_UFRONT_DEF_GL4ES})
# OpenGLDrv/UTGLR uses the reserved BUILD_NOPENGLDRV slot; USE_GL4ES (its WebGL backend) implies it.
if(USE_GL4ES)
  set(BUILD_NOPENGLDRV ON)
endif()
# Driver sources (XOpenGLDrv, OpenGLDrv/UTGLR) are FETCHED from their own repos, not vendored here;
# XOPENGL_SOURCE_DIR / UTGLR_SOURCE_DIR are set to the populated trees by the fetch block just above
# the "# Subprojects" section below. Only the per-driver build-glue CMakeLists.txt lives in-tree.


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

# ufront step 2.7b — Emscripten / WebAssembly build (the web port). Mirrors engine-unreal1's EMSCRIPTEN
# branch. Emscripten can't load UT99's .so packages, has no x86 asm, and gets SDL2/GLES2 from its own
# ports — so force the web-safe configuration here (the wasm/wasm-perf presets then need no extra -D flags).
if(EMSCRIPTEN)
  set(BUILD_STATIC ON)            # single binary via UNREAL_STATIC + --whole-archive (like Dreamcast)
  set(USE_SDL ON)                 # NSDLDrv (SDL2 window/GL context -> WebGL); provided by -sUSE_SDL=2
  set(LOW_MEMORY OFF)
  set(BUILD_EDITOR ON)            # ufront (bots parity): the stripped Editor package links only Engine+Core;
                                  # UT99 loads bot/character meshes+skins through Editor classes, so standalone
                                  # DM bots abort on package load without it (the engine-unreal1 "2.6f" finding
                                  # — "Included the Editor package"). Paired with the exception-safe UnObj.cpp
                                  # NULL-returns below (bot skins fail soft, incl. the absent BossSkins). The
                                  # Editor-compile fixes (PUSH_HIT sizeof->decltype, Min<INT>, UnParams
                                  # duplicate-guard) are already in-tree.
  set(BUILD_DCUTIL OFF)
  set(BUILD_PVRDRV OFF)           # Dreamcast render
  set(BUILD_AICADRV OFF)          # Dreamcast audio
  # (2.9) web render devices come from the platform-aware options above: XOpenGLDrv + OpenGLDrv/UTGLR
  # (via gl4es); NOpenGLESDrv dropped (glad clash). BUILD_NOPENGLDRV is driven by USE_GL4ES.
  set(BUILD_NULLSOUNDDRV ON)      # SoundDrv null (silent audio) — the config's shipped AudioDevice default
  set(BUILD_NOPENALDRV ON)        # ufront 2.12: real web+native audio — OpenAL SFX + libxmp .umx music.
                                  # The launcher (UnrealTournament/Src/Launch.cpp) overrides the ini's null
                                  # AudioDevice -> NOpenALDrv.NOpenALAudioSubsystem on Emscripten.
  add_definitions(-DASM=0)        # Core/Render use their portable (non-x86) code paths
  # ufront 2.7c: UE1 assumes Win32 /Zp4 struct packing — the reflected C++ class sizes must equal the
  # UnrealScript-declared property sizes (checked: sizeof(UFireTexture)==GetPropertiesSize()). On native
  # x86-32 the default layout happens to match, but wasm32 (like Android ARM32) does not — force it, per
  # UTA's fix. Applied to ALL sources so every class layout is uniform.
  add_compile_options(-fpack-struct=4 -Wno-address-of-packed-member)
  # clang (Emscripten) rejects passing a non-POD FString through variadic debugf/appErrorf as an ERROR;
  # GCC tolerates it. Downgrade to a warning (mostly script-export/debug paths). Mirrors engine-unreal1.
  add_compile_options(-Wno-error=non-pod-varargs -Wno-non-pod-varargs)
  # Exceptions: rely on Emscripten's default (exception CATCHING disabled). -fexceptions overflows V8's
  # call stack via invoke_ trampolines through UE1's deep package loader; -fwasm-exceptions is incompatible
  # with ASYNCIFY (the main-loop yield). Normal boot never throws; recoverable load failures are patched to
  # resolve to NULL one site at a time (step 2.7c, mirroring engine-unreal1/engine-ut2004); errors abort.
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

message(STATUS "Target arch: ${TARGET_ARCH}")
message(STATUS "64-bit: ${TARGET_IS_64BIT}")

if(TARGET_IS_64BIT)
  if(TARGET_IS_X86 AND NOT MSVC)
    message(STATUS "Building x86 binary with x86_64 compiler")
    set(TARGET_IS_64BIT FALSE)
    set(TARGET_ARCH "x86")
    add_compile_options(-m32)
  else()
    message(FATAL_ERROR "64-bit platforms are currently not supported. If you're building on Windows with MSVC, try -A Win32.")
  endif()
endif()

# Determine target OS/platform

set(TARGET_IS_WINDOWS FALSE)
set(TARGET_IS_UNIX FALSE)
set(TARGET_IS_DREAMCAST FALSE)
if(WIN32)
  set(TARGET_IS_WINDOWS TRUE)
else()
  # TODO
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

if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT)
else()
  add_definitions(-DPLATFORM_32BIT)
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
else()
  # TODO
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
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Og -g)
    add_link_options(-g)
  elseif(LOW_MEMORY)
    add_compile_options(-Os)
  else()
    add_compile_options(-O2)
  endif()
  add_compile_options(-ffunction-sections -fno-strict-aliasing)
  add_compile_options(-Wno-write-strings -Wno-terminate)
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
      set(SDL2_LIBRARY "-lSDL2")
    endif()
  endif()
endif()


# ufront — driver sources are FETCHED from their own GitHub repos (the 10-p forks), NOT vendored in
# this engine tree. The engine keeps only each driver's build-glue CMakeLists.txt (Source/XOpenGLDrv,
# Source/OpenGLDrv); the renderer source is pulled at configure time and XOPENGL_SOURCE_DIR /
# UTGLR_SOURCE_DIR are pointed at the populated trees (the build-glue then compiles/includes from
# there). Mirrors the gl4es block below. Neither repo ships a root CMakeLists.txt, so
# FetchContent_MakeAvailable only POPULATES (no add_subdirectory) — we drive the build ourselves.
#
# Dev workflow (edit a driver -> build -> push to 10-p -> bump *_GIT_TAG): point FetchContent at a
# local checkout with the standard override, e.g.
#     cmake --preset native -DFETCHCONTENT_SOURCE_DIR_XOPENGLDRV=/home/ion/engines/XOpenGLDrv
#     cmake --preset native -DFETCHCONTENT_SOURCE_DIR_UTGLR=/home/ion/engines/utglr-copy
# Default GIT_TAG is a branch (like gl4es); pin *_GIT_TAG to a commit for full reproducibility.
if(NOT PLATFORM_DREAMCAST AND BUILD_XOPENGLDRV)
  set(XOPENGL_GIT_REPOSITORY "https://github.com/10-p/XOpenGLDrv.git" CACHE STRING "XOpenGLDrv git repo (OldUnreal fork)")
  set(XOPENGL_GIT_TAG "ut99-v400-compat" CACHE STRING "XOpenGLDrv git tag/branch/commit")
  include(FetchContent)
  message(STATUS "XOpenGLDrv: fetching ${XOPENGL_GIT_REPOSITORY}@${XOPENGL_GIT_TAG}")
  FetchContent_Declare(XOpenGLDrv
    GIT_REPOSITORY "${XOPENGL_GIT_REPOSITORY}" GIT_TAG "${XOPENGL_GIT_TAG}" GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(XOpenGLDrv)
  set(XOPENGL_SOURCE_DIR "${xopengldrv_SOURCE_DIR}")
  message(STATUS "XOpenGLDrv: source = ${XOPENGL_SOURCE_DIR}")
endif()

if(NOT PLATFORM_DREAMCAST AND BUILD_NOPENGLDRV)
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

set(INSTALL_TARGETS Core Engine Render IpDrv Fire UWeb)

set(CMAKE_SHARED_LIBRARY_PREFIX "")

add_subdirectory(Core)
#add_subdirectory(Audio)
add_subdirectory(Engine)
add_subdirectory(Render)
add_subdirectory(IpDrv)
add_subdirectory(Fire)
# v436-compat port: build UWeb (WebRequest/WebResponse/WebServer native classes) — needed by
# JailBreak / UTServerAdmin. Was in-tree but unbuilt.
add_subdirectory(UWeb)
# ufront 2.12 Part B: ucc command-line tool (dedicated server / make / commandlets) — native-only executable.
if(NOT EMSCRIPTEN)
  add_subdirectory(UCC)
endif()

# ufront step 2.7d — the WebRTC net driver is a WEB-ONLY package (its transport is the glue webrtc_* ABI,
# baked via vendor/glue/webrtc-shim.js — not BSD sockets). Auto-switched from IpDrv.TcpNetDriver in Launch.cpp.
if(EMSCRIPTEN)
  add_subdirectory(WebRTCDrv)
  list(APPEND INSTALL_TARGETS WebRTCDrv)
endif()

if(USE_SDL)
  # SDL2
  add_subdirectory(NSDLDrv)
  list(APPEND INSTALL_TARGETS NSDLDrv)
else(TARGET_IS_DREAMCAST)
  # native Dreamcast driver
  add_subdirectory(KOSDrv)
  list(APPEND INSTALL_TARGETS KOSDrv)
endif()

# ufront 2.9 — static/wasm builds link under --whole-archive; NOpenGLESDrv and XOpenGLDrv both vendor a
# `glad` (duplicate globals), so they cannot co-link there. Native (.so) builds can carry both.
if(BUILD_STATIC AND BUILD_NOPENGLESDRV AND BUILD_XOPENGLDRV)
  message(FATAL_ERROR "NOpenGLESDrv and XOpenGLDrv cannot both be built into a static/wasm binary "
                      "(duplicate glad globals under --whole-archive). Enable at most one for static "
                      "(web ships XOpenGLDrv). See Source/XOpenGLDrv/ENGINE_CHANGES.md.")
endif()

if(BUILD_NOPENGLDRV)
  # gl4es (desktop GL -> GLES2) for the OpenGLDrv/UTGLR WebGL path. Fetched when USE_GL4ES (native gl4es
  # validation) or EMSCRIPTEN (the WebGL target). Source configurable via GL4ES_SOURCE_DIR. Mirrors
  # engine-ut2004/GL4ESDrv. See Source/OpenGLDrv/PORTING.md.
  if(USE_GL4ES OR EMSCRIPTEN)
    set(GL4ES_SOURCE_DIR "" CACHE PATH "Local gl4es checkout (vanilla or fork). Empty => fetch from git.")
    set(GL4ES_GIT_REPOSITORY "https://github.com/ptitSeb/gl4es.git" CACHE STRING "Upstream (vanilla) gl4es git repo")
    set(GL4ES_GIT_TAG "master" CACHE STRING "gl4es git tag/branch/commit to fetch (pin for reproducibility)")
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

# ufront 2.9 — XOpenGLDrv (OldUnreal GL -> WebGL2/GLES3). All v400 adaptations gated `#if ENGINE_VERSION==400`
# in the driver; see Source/XOpenGLDrv/ENGINE_CHANGES.md.
if(NOT PLATFORM_DREAMCAST AND BUILD_XOPENGLDRV)
  # Build glue lives in-tree (Source/XOpenGLDrv); it compiles/includes from ${XOPENGL_SOURCE_DIR} (fetched).
  add_subdirectory(XOpenGLDrv)
  list(APPEND INSTALL_TARGETS XOpenGLDrv)
endif()

# ufront (ut99 software renderer): SoftDrv (CPU rasterizer) ported from the original UT99 v400 source.
# Pure software (no glad) -> coexists with XOpenGL/UTGLR on web (not subject to the glad-clash guard);
# selected via -SOFTWARERENDERER (the launcher switch table already names SoftDrv.SoftwareRenderDevice).
if(NOT PLATFORM_DREAMCAST AND BUILD_SOFTDRV)
  add_subdirectory(SoftDrv)
  list(APPEND INSTALL_TARGETS SoftDrv)
endif()

# ufront (null render device): NullDrv (no-op URenderDevice) — GPU-free, no GL context. Selected via
# -NULLRENDERER for a headless / "poor-man's dedicated server" run (host listens, empty tab). Additive like
# SoftDrv; the launcher switch table names NullDrv.NullRenderDevice.
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

# ufront 2.12 (web + native audio): NOpenALDrv — OpenAL SFX + a FetchContent'd libxmp static lib for .umx
# tracker music, pumped from Update() (single music path, no background thread). Ported from engine-unreal1,
# adapted to UT99 v400's UAudioSubsystem. Selected by the launcher on Emscripten (-> NOpenALAudioSubsystem).
if(BUILD_NOPENALDRV)
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