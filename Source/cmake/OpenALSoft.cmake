# OpenALSoftWeb.cmake — openal-soft for the WEB build (ufront 2.61), replacing Emscripten's built-in OpenAL
# (-lopenal / libopenal.js). Vendored identically in every UE1 engine repo; engine-ut2004 carries the same
# block inline in its root CMakeLists.
#
# Why: libopenal.js is stereo-only by construction (every source goes through a PannerNode), has no EFX,
# and all 93 of its functions are __proxy:'sync' (every AL call is a blocking round trip to the browser
# main thread). openal-soft is the same API, mixed in wasm on its own pthread and played by an AudioWorklet
# (the `web` backend on the ufront/web branch, alc/backends/web.cpp): 5.1/7.1 where the device has it,
# HRTF on request, EFX reverb, and the launcher's audio buffer settings (Module.alsoftConfig).
#
#   ufront_openal_soft_web(EAX <ON|OFF>)   -> defines the static target `OpenAL` (+ its public include dir)
#
# ★ WHAT THIS HAS TO GET RIGHT (ufront 2.61 §1):
#  1. The engine's flags are not openal-soft's. Directory compile options (UE1's -fpack-struct=4 above all:
#     libc++'s structs would stop matching) are stripped from its targets after they exist, and the
#     engine's clang-compat C++ flags are dropped from CMAKE_CXX_FLAGS for its configure. openal-soft pins
#     its own CXX_STANDARD 20 per target.
#  2. EXCEPTIONS. openal-soft reports every AL error by THROWING and catching it at the API boundary. Its
#     objects are compiled with Emscripten's JS exceptions (-fexceptions) and the engine link must add
#     -sNO_DISABLE_EXCEPTION_CATCHING; the engine's own objects stay -fignore-exceptions (no invoke_
#     trampolines through the package loader). Wasm-native exceptions cannot coexist with ASYNCIFY.
#  3. SIMD: its hand-written SSE mixers compile through Emscripten's translation headers (-msimd128
#     -msse4.1). NEON must be OFF: its header check passes on Emscripten and the runtime would prefer it.
#
# Call it AFTER the engine's global -pthread compile option (a shared-memory link needs atomics/bulk-memory
# on every object) and before the target that links `OpenAL`.
function(ufront_openal_soft_web)
  cmake_parse_arguments(ARG "" "EAX" "" ${ARGN})
  if(NOT ARG_EAX)
    set(ARG_EAX OFF)
  endif()
  include(FetchContent)
  set(OPENALSOFT_GIT_REPOSITORY "https://github.com/10-p/openal-soft-web.git"
      CACHE STRING "openal-soft git remote; its ufront/web branch carries the Web Audio backend")
  set(OPENALSOFT_GIT_TAG "ufront/web" CACHE STRING "openal-soft git ref")
  FetchContent_Declare(
    OpenALSoft
    GIT_REPOSITORY ${OPENALSOFT_GIT_REPOSITORY}
    GIT_TAG        ${OPENALSOFT_GIT_TAG}
    GIT_SHALLOW    TRUE
  )
  # Function scope: these reach the subproject's configure and die with the function.
  string(REGEX REPLACE "-fdelayed-template-parsing|-Wno-writable-strings|-Wno-reserved-user-defined-literal|-Wno-incompatible-pointer-types"
         "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(APPEND CMAKE_CXX_FLAGS " -msimd128 -msse4.1 -fexceptions")
  string(APPEND CMAKE_C_FLAGS " -msimd128 -msse4.1")
  set(LIBTYPE STATIC CACHE STRING "" FORCE)
  foreach(_al_opt ALSOFT_UTILS ALSOFT_EXAMPLES ALSOFT_TESTS ALSOFT_INSTALL ALSOFT_INSTALL_CONFIG
          ALSOFT_INSTALL_HRTF_DATA ALSOFT_INSTALL_AMBDEC_PRESETS ALSOFT_BACKEND_WAVE ALSOFT_CPUEXT_NEON
          ALSOFT_DLOPEN ALSOFT_UPDATE_BUILD_VERSION)
    set(${_al_opt} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(ALSOFT_EAX ${ARG_EAX} CACHE BOOL "" FORCE)
  set(ALSOFT_BACKEND_WEB ON CACHE BOOL "" FORCE)
  set(ALSOFT_REQUIRE_SSE4_1 ON CACHE BOOL "" FORCE)  # a silent fallback to the C mixers is 2.5x slower
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
  message(STATUS "openal-soft (web): ${OPENALSOFT_GIT_REPOSITORY}@${OPENALSOFT_GIT_TAG}, EAX=${ARG_EAX}")
endfunction()
