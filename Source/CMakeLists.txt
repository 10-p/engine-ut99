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

# Web build mode (EMSCRIPTEN only) — see the three-mode block further down. UT_WEB_PERF is read by
# UnrealTournament/CMakeLists.txt for the link flags; UT_WEB_DEBUG selects -O0 over the -O2 dev default.
option(UT_WEB_PERF "Emscripten: shipping build — link at -O3 -g0, assertions off" OFF)
option(UT_WEB_DEBUG "Emscripten: -O0 instead of the -O2 dev default (nothing inlined, every local live)" OFF)
option(USE_SDL "Use NSDLDrv instead of WinDrv" ON)
option(LOW_MEMORY "Conserve memory as much as possible" OFF)
option(BUILD_UNREALTOURNAMENT "Build the launcher" ON)
# ★ ufront 2.39 — BUILD_EDITOR IS NOT OPTIONAL FOR A PLAYABLE GAME, on any platform.
#
# UE1 ships game CONTENT that references classes living in the Editor package, so "the editor" here is
# not a tool you are choosing to build — it is a runtime dependency of maps and meshes. The web build has
# forced it ON since 2.6f; the NATIVE and WINDOWS presets still said OFF, and the symptom is exactly what
# the owner reported:
#
#     Warning: Failed to load 'Editor': Can't find Class Editor.TransBuffer in file '../System/Editor.u'
#     Warning: Failed to load 'Level None.MyLevel':
#
# — Dug.unr silently falls back to the default map. (Editor.u, the SCRIPT half, ships with the game; it is
# the NATIVE half that was missing. Same shape as UT99's UWeb crash in 2.38.) The Editor built here is the
# stripped, no-GUI NullEdHook variant that links only Engine+Core, so this costs a static library and
# nothing at runtime. Verified natively: before, Dug fell back to Unreal.unr; after,
# "Bringing Level Dug.MyLevel up for play".
option(BUILD_EDITOR "Build the Editor package (a RUNTIME dependency — see above)" ON)
option(BUILD_NOPENGLDRV "Build NOpenGLDrv" OFF)
option(BUILD_PVRDRV "Build PVRDrv (Dreamcast only)" OFF)
option(BUILD_NULLSOUNDDRV "Build SoundDrv (Null driver)" ON)
option(BUILD_NOPENALDRV "Build NOpenALDrv (OpenAL SFX + libxmp .umx music; web + native Linux)" OFF)
option(BUILD_AICADRV "Build AICADrv (Dreamcast AICA sound driver)" OFF)
option(BUILD_DCUTIL "Build DCUtil" ON)
option(BUILD_STATIC "Link everything into a single binary" OFF)

# ufront 2.9 — HW render devices: orthogonal per-renderer toggles + configurable source folders.
# NOpenGLESDrv (GLES2/WebGL1) is the historical device; XOpenGLDrv (OldUnreal, WebGL2/GLES3) and
# OpenGLDrv/UTGLR (via gl4es, WebGL1) are the 2.9 additions.
# ufront 2.19 — web now ships ALL THREE. NOpenGLES used to be dropped on web because it and XOpenGLDrv
# both vendored a `glad` with identical symbol names, which cannot co-link under --whole-archive; it no
# longer compiles one on Emscripten (its 45 entry points are all core GLES2, linked in by -sFULL_ES2), so
# the clash is gone at the source rather than worked around. It is a FALLBACK, never a default: XOpenGL
# stays the web default. Native (.so) defaults to NOpenGLES only. Any combo is selectable via -D.
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
  # (2.9 / 2.19) web render devices come from the platform-aware options above: XOpenGLDrv (default) +
  # OpenGLDrv/UTGLR (via gl4es) + NOpenGLESDrv (2.19 — the glad clash is fixed at the source, so it is a
  # selectable fallback). BUILD_NOPENGLDRV is driven by USE_GL4ES.
  # ⚠ Forced with set(), not left to option(): option() only supplies a DEFAULT, so an existing build tree
  # that was configured before 2.19 keeps its cached BUILD_NOPENGLESDRV=OFF and silently builds without the
  # driver — the binary then links fine and the engine reports "not compiled in this build" at runtime.
  set(BUILD_NOPENGLESDRV ON)
  set(BUILD_NULLSOUNDDRV ON)      # SoundDrv null (silent audio) — the config's shipped AudioDevice default
  set(BUILD_NOPENALDRV ON)        # ufront 2.12: real web+native audio — OpenAL SFX + libxmp .umx music.
                                  # The launcher (UnrealTournament/Src/Launch.cpp) overrides the ini's null
                                  # AudioDevice -> NOpenALDrv.NOpenALAudioSubsystem on Emscripten.
  add_definitions(-DASM=0)        # Core/Render use their portable (non-x86) code paths
  # ufront 2.13.2 — -pthread must be a COMPILE option, not just a link option. It defines
  # __EMSCRIPTEN_PTHREADS__, which is what the threaded code paths (NSDLViewport's direct WebGL context
  # creation, XOpenGL's entry-point resolver, appRequestExit's MAIN_THREAD_EM_ASM) are guarded on; with it
  # only at link time those guards silently compile their SINGLE-THREADED branch, SDL creates a GL context
  # anyway, and Emscripten proxies eglCreateContext to a main thread whose canvas has been transferred away
  # -> InvalidStateError (Emscripten #24792). It also gives every object — including the FetchContent'd
  # gl4es, XOpenGL and utglr trees — the atomics/bulk-memory features a shared-memory link expects.
  add_compile_options(-pthread)
  # ufront 2.7c: UE1 assumes Win32 /Zp4 struct packing — the reflected C++ class sizes must equal the
  # UnrealScript-declared property sizes (checked: sizeof(UFireTexture)==GetPropertiesSize()). On native
  # x86-32 the default layout happens to match, but wasm32 (like Android ARM32) does not — force it, per
  # UTA's fix. Applied to ALL sources so every class layout is uniform.
  add_compile_options(-fpack-struct=4 -Wno-address-of-packed-member)
  # clang (Emscripten) rejects passing a non-POD FString through variadic debugf/appErrorf as an ERROR;
  # GCC tolerates it. Downgrade to a warning (mostly script-export/debug paths). Mirrors engine-unreal1.
  add_compile_options(-Wno-error=non-pod-varargs -Wno-non-pod-varargs)
  # Exceptions: the ENGINE's objects rely on Emscripten's default (-fignore-exceptions). -fexceptions overflows
  # V8's call stack via invoke_ trampolines through UE1's deep package loader; -fwasm-exceptions is incompatible
  # with ASYNCIFY (the main-loop yield). Normal boot never throws; recoverable load failures are patched to
  # resolve to NULL one site at a time (step 2.7c, mirroring engine-unreal1/engine-ut2004); errors abort.
  # ufront 2.61: openal-soft is the one exception to that. It reports every AL error by throwing, so ITS objects
  # are -fexceptions (cmake/OpenALSoftWeb.cmake) and the link enables catching (UnrealTournament/CMakeLists.txt);
  # nothing of the engine's gains an invoke_.
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
  if(MINGW)
    # ufront 2.33 — link GCC's runtime statically. Without this the .exe imports libgcc_s_dw2-1.dll and
    # libstdc++-6.dll, which live in the toolchain (or MSYS2's /mingw32/bin), NOT in a Windows install:
    # the binary runs on the build machine and dies with "not found" everywhere else. With it, the only
    # imports left are stock system DLLs, so the static build really is a single self-contained file.
    add_link_options(-static-libgcc -static-libstdc++)
    # ufront 2.33 — UE1 assumes Win32 /Zp4 struct packing: every reflected C++ class size must equal the
    # UnrealScript-declared property size. Native x86-32 Linux happens to match because its ABI aligns
    # double/int64 to 4; MinGW follows the MSVC ABI and aligns them to 8, so every class with a double
    # came out a few bytes long (Mover 1060 vs 1056, FireTexture 1296 vs 1292 …) and member offsets
    # shifted. The MSVC branch above passes /Zp4 for the same reason, and the web build -fpack-struct=4.
    # ★ Caught by 2.32's per-class layout guard, which reported every offending class by name before the
    # engine got anywhere near gameplay — a silent version of this is what 2.32 existed to hunt down.
    # Applied to ALL sources: packing must be uniform or the two halves of a class disagree.
    add_compile_options(-fpack-struct=4 -Wno-address-of-packed-member)
    # Windows puts in libraries what glibc keeps in libc: sockets (Core's appComputerName/appGetLocalIP
    # and all of IpDrv) are ws2_32, and appGetGUID's CoCreateGuid is ole32. Declared here, before the
    # add_subdirectory calls, so every package inherits them.
    link_libraries(ws2_32 ole32)
  endif()
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
  if(EMSCRIPTEN)
    # ★ THE THREE WEB BUILD MODES (ufront 2.19d), identical in all four engine repos:
    #
    #   preset        UT_WEB_PERF  UT_WEB_DEBUG  compile   what it is for
    #   wasm-perf     ON           -             -O2 (*)   shipping: link-time -O3 -g0, no assertions
    #   wasm          OFF          OFF           -O2 -g    THE DEV DEFAULT: named stacks, DWARF, assertions
    #   wasm-debug    OFF          ON            -O0 -g    last resort: every local live, nothing inlined
    #
    # (*) This engine's perf build differs from the dev build at LINK only (see UnrealTournament/
    #     CMakeLists.txt); ut2004 additionally compiles its perf build at -O3. That divergence is recorded,
    #     not fixed here — changing it would alter a shipping build's codegen and needs its own verification.
    #
    # -O0 is what you switch to when the OPTIMISER is the suspect: a bug that appears at one -O level and
    # not another is almost always undefined behaviour rather than a logic error. It is NOT the smaller
    # build — measured on ut2004, -O2 -g came out LARGER than -O0 -g, because DWARF forces Emscripten to
    # "run only limited binaryen optimizations" so -O2's inlining is never cleaned up. Speed is what the
    # dev default buys, not size.
    if(UT_WEB_DEBUG)
      add_compile_options(-O0 -g)
    elseif(UT_WEB_PERF)
      # UNCHANGED from before the three-mode split: the shipping build carries no compile-side DWARF.
      # (It is -O2 at compile and -O3 -g0 at link — see UnrealTournament/CMakeLists.txt. Adding -g here
      # would grow every object and make emcc "run only limited binaryen optimizations".)
      add_compile_options(-O2)
    else()
      add_compile_options(-O2 -g)
    endif()
  # ★ ufront 2.54 — CHECKED, AND THIS ENGINE IS CLEAN; DO NOT "TIDY" IT. CMake injects
  # CMAKE_<LANG>_FLAGS_<CONFIG> ahead of anything add_compile_options() appends and the compiler takes
  # the LAST -O, which on engine-ut2004 silently compiled every native build at -O0 (its native branch
  # appended -O0 to every configuration). Here the appended level MATCHES the injected one, so a
  # RelWithDebInfo TU reads `-O2 … -O2` — redundant on the line, identical in effect. Measured with
  # compile_commands.json rather than reasoned about.
  #
  # ⚠ AND THE -Og YOU WILL SEE ON TWO FILES IS DELIBERATE, NOT THIS BUG. Core/CMakeLists.txt lowers
  # UnCorSc.cpp and UnProp.cpp on purpose: gcc miscompiles UE's FFrame::Step() reference juggling at a
  # higher level. Those two are supposed to win.
  #
  # ⚠ The per-config variables are deliberately NOT blanked here. They are where -DNDEBUG comes from on
  # this engine — the branches below never add it — so clearing them to remove a harmless duplicate -O
  # would switch assert() back on across the whole engine for no gain.
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
        # ufront 2.13.2 — the -sUSE_SDL=2 PORT supplies SDL2 (and picks the right variant per build:
        # libSDL2.a normally, libSDL2-mt.a under -pthread). An explicit -lSDL2 on top of that resolves to
        # the NON-threaded archive, and the threaded link then dies with
        #   "--shared-memory is disallowed by SDL_atomic.c.o because it was not compiled with 'atomics'"
        # because the two archives disagree about features. Leave it to the port.
        set(SDL2_LIBRARY )
      elseif(MINGW AND CMAKE_CROSSCOMPILING)
        # ufront 2.33 — cross-building for Windows from Linux: no system SDL2 to link against, so fetch
        # SDL2's official prebuilt MinGW binaries. Under MSYS2 on real Windows this branch is skipped
        # (not cross-compiling) and pacman's mingw-w64-i686-SDL2 answers -lSDL2 as usual.
        include(SDL2Mingw)
      else()
        set(SDL2_LIBRARY "-lSDL2")
      endif()
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

# ufront 2.38 — UWeb joins the list. The game has always SHIPPED UWeb.u; without its natives compiled,
# loading that package is a hard `Can't bind to native class UWeb.WebRequest` appError — which is what
# killed "Start New Multiplayer Game". See UWeb/CMakeLists.txt.
set(INSTALL_TARGETS Core Engine Render IpDrv UWeb Fire)

set(CMAKE_SHARED_LIBRARY_PREFIX "")

add_subdirectory(Core)
#add_subdirectory(Audio)
add_subdirectory(Engine)
add_subdirectory(Render)
add_subdirectory(IpDrv)
add_subdirectory(UWeb)
add_subdirectory(Fire)
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
elseif(TARGET_IS_DREAMCAST)
  # ufront 2.33: this was `else(TARGET_IS_DREAMCAST)`, which CMake reads as a plain else() with a decorative
  # argument — so ANY non-SDL build (a Windows one, say) silently selected the Dreamcast viewport driver
  # and failed inside KOSDrv with "KOSDrv is for Dreamcast only".
  # native Dreamcast driver
  add_subdirectory(KOSDrv)
  list(APPEND INSTALL_TARGETS KOSDrv)
endif()

# ufront 2.9 / 2.19 / 2.34 — static builds link under --whole-archive, so every object of every driver
# is force-included. NOpenGLESDrv and XOpenGLDrv each vendor a `glad`, and the two generators emit
# byte-identical external names for everything GLES2 and desktop GL share (glad_glActiveTexture, …) —
# a duplicate-definition link failure that used to make the combination illegal outside Emscripten:
#
#   2.19 fixed it for the WEB at the source: NOpenGLESDrv compiles no glad there (Emscripten links the
#        GLES2 entry points in directly), so the web has shipped both drivers ever since.
#   2.34 fixes it EVERYWHERE ELSE, because the single-exe Linux/Windows targets need the same thing:
#        Thirdparty/glad_es now renames its externals UTGLES_* (Thirdparty/glad_es/glad_symbol_prefix.h),
#        so the two loaders simply no longer collide. The FATAL_ERROR that stood here is gone with it —
#        `native` is now a static build carrying ALL FIVE renderers, which is what kept the compat
#        oracle (NOpenGLES, the web renderer's twin) available after the switch to one binary.

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
  if(EMSCRIPTEN)
    # ufront 2.61: openal-soft instead of Emscripten's built-in OpenAL (5.1/HRTF/EFX reverb on the web).
    # EAX OFF: NOpenALDrv never calls the EAX API — its "EAX" reverb is EFX's AL_EFFECT_EAXREVERB, which
    # openal-soft always has. Flip it if a driver ever needs EAXSet/EAXGet (engine-ut2004's ALAudio does).
    include(OpenALSoftWeb)
    ufront_openal_soft_web(EAX OFF)
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