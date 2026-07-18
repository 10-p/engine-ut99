project(NOpenALDrv C CXX)

set(SRC_FILES "NOpenALDrv.cpp")

# ufront 2.12 (web + native audio): SFX via OpenAL — Emscripten's built-in (Web Audio-backed, OpenAL 1.1
# core only; the driver compiles out EFX/HRTF under NOPENAL_WEB, linked via -lopenal) on the web, and
# openal-soft on native Linux (full EFX/reverb/HRTF). libxmp (the tracker decoder for Unreal's .umx music)
# is not reliably shipped by either toolchain, so fetch + build it as a static lib on BOTH targets — it is
# portable C and cross-compiles cleanly under emcc and gcc. Depackers/ProWizard are disabled (Unreal music
# is raw modules; depackers pull archive deps + bloat). NOTE: libxmp's BUILD_STATIC option collides by name
# with the engine's own BUILD_STATIC ("single binary"); under libxmp's capped policy (CMP0077 OLD) its
# option() would reset the inherited value and disable its static build (-> its "both disabled" FATAL_ERROR),
# so we FORCE the cache vars here (ON matches the engine's static/Emscripten intent). Reused verbatim from
# engine-unreal1's NOpenALDrv (web) + extended to the native branch below.
include(FetchContent)
set(LIBXMP_SOURCE_DIR "" CACHE PATH "Local libxmp checkout. Empty => fetch from git.")
set(LIBXMP_GIT_REPOSITORY "https://github.com/libxmp/libxmp.git" CACHE STRING "Upstream libxmp git repo")
set(LIBXMP_GIT_TAG "libxmp-4.7.1" CACHE STRING "libxmp git tag/commit to fetch (pin for reproducibility)")
if(LIBXMP_SOURCE_DIR)
  message(STATUS "libxmp: using local source ${LIBXMP_SOURCE_DIR}")
  FetchContent_Declare(libxmp SOURCE_DIR "${LIBXMP_SOURCE_DIR}")
else()
  message(STATUS "libxmp: fetching ${LIBXMP_GIT_REPOSITORY}@${LIBXMP_GIT_TAG}")
  FetchContent_Declare(libxmp GIT_REPOSITORY "${LIBXMP_GIT_REPOSITORY}" GIT_TAG "${LIBXMP_GIT_TAG}" GIT_SHALLOW TRUE)
endif()
set(BUILD_STATIC ON  CACHE BOOL "" FORCE)   # libxmp static lib (see note above re: name collision)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_LITE   OFF CACHE BOOL "" FORCE)
set(LIBXMP_DISABLE_DEPACKERS ON CACHE BOOL "" FORCE)
set(LIBXMP_DISABLE_PROWIZARD ON CACHE BOOL "" FORCE)
set(WITH_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)  # libxmp's old cmake_minimum_required vs CMake 4.x
FetchContent_MakeAvailable(libxmp)
unset(CMAKE_POLICY_VERSION_MINIMUM)
if(NOT TARGET xmp_static)
  message(FATAL_ERROR "libxmp: expected static target 'xmp_static' was not created.")
endif()
set(LIBXMP_LIBRARY xmp_static)

if(EMSCRIPTEN)
  # Emscripten's OpenAL (built-in) — the fetched libxmp static target overrides any parent -lxmp.
  set(OPENAL_LIBRARY "-lopenal")
else()
  # Native (Linux): system OpenAL (openal-soft) provides the full feature set (EFX/reverb/HRTF).
  find_package(OpenAL)
  if(NOT OPENAL_LIBRARY)
    set(OPENAL_LIBRARY "-lopenal")   # fall back to a plain link if find_package didn't populate it
  endif()
endif()

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${OPENAL_INCLUDE_DIR}
  ${LIBXMP_INCLUDE_DIR}
)

if(MSVC)
  # openal-soft release libs are built without safeseh for some reason?
  target_link_options(${PROJECT_NAME} PRIVATE "/SAFESEH:NO")
endif()

target_link_libraries(${PROJECT_NAME} ${OPENAL_LIBRARY} ${LIBXMP_LIBRARY})

target_link_libraries(${PROJECT_NAME} Engine Render Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE NOPENALDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
