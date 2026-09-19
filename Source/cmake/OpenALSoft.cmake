# OpenALSoft.cmake — openal-soft, the ufront fork, for the WEB build (ufront 2.61) and for native LINUX
# (ufront 2.70). Vendored identically in every UE1 engine repo; engine-ut2004 carries the same two blocks
# inline in its root CMakeLists. Was OpenALSoftWeb.cmake until 2.70.
#
#   ufront_openal_soft(PLATFORM <WEB|NATIVE> EAX <ON|OFF>)   -> defines the target `OpenAL`
#     WEB    a STATIC archive linked into the wasm, mixed on its own pthread and played by an AudioWorklet
#            (the `web` backend on the fork's ufront/web branch): 5.1/7.1 where the device has it, HRTF on
#            request, EFX reverb, the launcher's audio buffer settings (Module.alsoftConfig).
#     NATIVE a SHARED libopenal.so.1 that NOpenALDrv links (DT_NEEDED) and the launcher copies beside the
#            binary, found through $ORIGIN. Why not the system libopenal: it is whatever the distro built
#            (Ubuntu's has no EAX), so the native binary carries the fork to play what the web plays.
#            Backends are whatever the host's headers allowed at configure time (PulseAudio + ALSA on this
#            box), each dlopen'd at run time.
#
# The fork: https://github.com/10-p/openal-soft-web `ufront/web` = openal-soft 1.25.2 + the Web Audio backend
# + the wasm build patches (harmless natively: all under __EMSCRIPTEN__). A BRANCH, on purpose — a rebuild
# takes its tip (the owner's rule; see cmake/ArtifactDeps.cmake, which records it in meta.json).
#
# ★ WHAT THIS HAS TO GET RIGHT (ufront 2.61 §1):
#  1. The engine's flags are not openal-soft's. Directory compile options (UE1's -fpack-struct=4 above all:
#     libc++'s structs would stop matching) are stripped from its targets after they exist, and the
#     engine's clang-compat C++ flags are dropped from CMAKE_CXX_FLAGS for its configure. openal-soft pins
#     its own CXX_STANDARD 20 per target.
#  2. EXCEPTIONS (web). openal-soft reports every AL error by THROWING and catching it at the API boundary.
#     Its wasm objects are compiled with Emscripten's JS exceptions (-fexceptions) and the engine link must
#     add -sNO_DISABLE_EXCEPTION_CATCHING; the engine's own objects stay -fignore-exceptions (no invoke_
#     trampolines through the package loader). Wasm-native exceptions cannot coexist with ASYNCIFY.
#     Natively g++ has exceptions on, so nothing to do.
#  3. SIMD (web): its hand-written SSE mixers compile through Emscripten's translation headers (-msimd128
#     -msse4.1). NEON must be OFF: its header check passes on Emscripten and the runtime would prefer it.
#     Natively the mixers are selected by cpuid at run time, at either pointer width (-m32 included).
#
# Call it AFTER the engine's global compile options (a shared-memory web link needs atomics/bulk-memory on
# every object) and before the target that links `OpenAL`.
function(ufront_openal_soft)
  cmake_parse_arguments(ARG "" "PLATFORM;EAX" "" ${ARGN})
  if(NOT ARG_EAX)
    set(ARG_EAX OFF)
  endif()
  if(NOT ARG_PLATFORM MATCHES "^(WEB|NATIVE)$")
    message(FATAL_ERROR "ufront_openal_soft: PLATFORM must be WEB or NATIVE (got '${ARG_PLATFORM}')")
  endif()
  include(FetchContent)
  set(OPENALSOFT_GIT_REPOSITORY "https://github.com/10-p/openal-soft-web.git"
      CACHE STRING "openal-soft git remote; its ufront/web branch carries the Web Audio backend")
  set(OPENALSOFT_GIT_TAG "ufront/web" CACHE STRING "openal-soft git ref (a branch: a rebuild takes its tip)")
  FetchContent_Declare(
    OpenALSoft
    GIT_REPOSITORY ${OPENALSOFT_GIT_REPOSITORY}
    GIT_TAG        ${OPENALSOFT_GIT_TAG}
    GIT_SHALLOW    TRUE
  )
  # Function scope: these reach the subproject's configure and die with the function.
  string(REGEX REPLACE "-fdelayed-template-parsing|-Wno-writable-strings|-Wno-reserved-user-defined-literal|-Wno-incompatible-pointer-types"
         "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  if(ARG_PLATFORM STREQUAL "WEB")
    string(APPEND CMAKE_CXX_FLAGS " -msimd128 -msse4.1 -fexceptions")
    string(APPEND CMAKE_C_FLAGS " -msimd128 -msse4.1")
    set(LIBTYPE STATIC CACHE STRING "" FORCE)
    set(ALSOFT_DLOPEN OFF CACHE BOOL "" FORCE)
    set(ALSOFT_CPUEXT_NEON OFF CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_WEB ON CACHE BOOL "" FORCE)
    set(ALSOFT_REQUIRE_SSE4_1 ON CACHE BOOL "" FORCE)  # a silent fallback to the C mixers is 2.5x slower
  else()
    set(LIBTYPE SHARED CACHE STRING "" FORCE)           # -> libopenal.so.1, beside the binary
    set(ALSOFT_DLOPEN ON CACHE BOOL "" FORCE)           # backends load libpulse/libasound at run time
  endif()
  foreach(_al_opt ALSOFT_UTILS ALSOFT_EXAMPLES ALSOFT_TESTS ALSOFT_INSTALL ALSOFT_INSTALL_CONFIG
          ALSOFT_INSTALL_HRTF_DATA ALSOFT_INSTALL_AMBDEC_PRESETS ALSOFT_BACKEND_WAVE
          ALSOFT_UPDATE_BUILD_VERSION)
    set(${_al_opt} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(ALSOFT_EAX ${ARG_EAX} CACHE BOOL "" FORCE)
  set(ALSOFT_EMBED_HRTF_DATA ON CACHE BOOL "" FORCE)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
  FetchContent_MakeAvailable(OpenALSoft)

  # Strip the engine's directory-level compile options that must not reach openal-soft (see 1. above) —
  # from every target in its tree, SUBDIRECTORIES included: its bundled fmt is one, and a first version that
  # walked only the top directory left fmt compiled with -fpack-struct=4.
  set(_al_dirs "${openalsoft_SOURCE_DIR}")
  while(_al_dirs)
    list(POP_FRONT _al_dirs _dir)
    get_property(_subdirs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    list(APPEND _al_dirs ${_subdirs})
    get_property(_al_targets DIRECTORY "${_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t ${_al_targets})
      get_target_property(_type ${_t} TYPE)
      if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
        continue()
      endif()
      get_target_property(_opts ${_t} COMPILE_OPTIONS)
      if(_opts)
        list(REMOVE_ITEM _opts -fpack-struct=4 -Wno-address-of-packed-member)
        set_target_properties(${_t} PROPERTIES COMPILE_OPTIONS "${_opts}")
      endif()
    endforeach()
  endwhile()
  if(NOT TARGET OpenAL)
    message(FATAL_ERROR "openal-soft: expected target 'OpenAL' was not created")
  endif()
  if(ARG_PLATFORM STREQUAL "NATIVE")
    # UE1 builds its own modules without the `lib` prefix (Engine.so, NOpenALDrv.so) and the subproject
    # inherits that, which would make this `openal.so.1`. Keep the name the world knows.
    set_target_properties(OpenAL PROPERTIES PREFIX "lib")
  endif()
  message(STATUS "openal-soft (${ARG_PLATFORM}): ${OPENALSOFT_GIT_REPOSITORY}@${OPENALSOFT_GIT_TAG}, EAX=${ARG_EAX}")
endfunction()
