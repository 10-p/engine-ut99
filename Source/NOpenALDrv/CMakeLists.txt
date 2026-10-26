project(NOpenALDrv C CXX)

set(SRC_FILES "NOpenALDrv.cpp")

# Sound through OpenAL (the openal-soft fork, cmake/OpenALSoft.cmake), music through libxmp, the tracker
# decoder for .umx, fetched and built as a static library on every target. Depackers and ProWizard are off
# (Unreal music is raw modules). libxmp's BUILD_STATIC option shares its name with the engine's: under
# libxmp's CMP0077 OLD policy its option() would reset the inherited value, so the cache variable is forced
# for its configure and the engine's restored afterwards (the save/restore around BUILD_STATIC below).
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
# Save the engine's cached BUILD_STATIC, force it ON for libxmp's configure, and restore it after
# MakeAvailable (below), so a reconfigure of this build dir still reads the engine's own value. Under
# CMP0077 OLD the value must be in the cache before libxmp's option().
set(_UFRONT_SAVED_BUILD_STATIC "$CACHE{BUILD_STATIC}")
set(BUILD_STATIC ON  CACHE BOOL "" FORCE)   # libxmp static lib (restored after MakeAvailable)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_LITE   OFF CACHE BOOL "" FORCE)
set(LIBXMP_DISABLE_DEPACKERS ON CACHE BOOL "" FORCE)
set(LIBXMP_DISABLE_PROWIZARD ON CACHE BOOL "" FORCE)
set(WITH_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)  # libxmp's old cmake_minimum_required vs CMake 4.x
FetchContent_MakeAvailable(libxmp)
unset(CMAKE_POLICY_VERSION_MINIMUM)
# Restore the engine's BUILD_STATIC so a reconfigure of this build dir isn't poisoned by libxmp's FORCE.
set(BUILD_STATIC "${_UFRONT_SAVED_BUILD_STATIC}" CACHE BOOL "Link everything into a single binary" FORCE)
if(NOT TARGET xmp_static)
  message(FATAL_ERROR "libxmp: expected static target 'xmp_static' was not created.")
endif()
set(LIBXMP_LIBRARY xmp_static)

if(EMSCRIPTEN)
  # openal-soft, built by the parent (cmake/OpenALSoft.cmake WEB); the target also brings its <AL/alext.h> and
  # <AL/efx.h>.
  set(OPENAL_LIBRARY OpenAL)
elseif(TARGET OpenAL)
  # Native Linux: the parent built the same fork as a shared library (cmake/OpenALSoft.cmake NATIVE); the
  # launcher copies libopenal.so.1 beside the binary and links with $ORIGIN.
  set(OPENAL_LIBRARY OpenAL)
elseif(MINGW AND CMAKE_CROSSCOMPILING)
  # Cross-building for Windows: OpenAL Soft's official prebuilt Windows binaries (cmake/OpenALMingw.cmake).
  include(OpenALMingw)
else()
  # Native without the bundled fork: the system OpenAL.
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

if(ANDROID)
  # SDL's application events pause the output device while the app is in the background.
  target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIR})
  target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY})
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE NOPENALDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
