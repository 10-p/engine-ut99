project(XOpenGLDrv C CXX)

# XOpenGLDrv — build glue for OldUnreal's OpenGL renderer, fetched from its v400 fork. The fork gates its
# v400 adaptations behind `#if ENGINE_VERSION==400`; see ENGINE_CHANGES.md.
#
# On v400 the device creates no GL context of its own: it reuses the one NSDLViewport makes current, so it
# needs SDL/NSDLDrv.
if(NOT USE_SDL)
  message(FATAL_ERROR "XOpenGLDrv requires NSDLDrv (SDL) to function.")
endif()

# All Src/*.cpp are standalone translation units (the *_GLSL.cpp shader files and the fork's desktop-GL
# glad.cpp loader included), from the fetched ${XOPENGL_SOURCE_DIR}.
file(GLOB SRC_FILES "${XOPENGL_SOURCE_DIR}/Src/*.cpp")

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

# Its own include set: the fork's desktop-GL `glad.h`/`glext.h` (Inc/) and glm (glm/), not the engine's
# Thirdparty/glad_es or trimmed Thirdparty/glm; the Render private headers come from ../Render/Src.
target_include_directories(${PROJECT_NAME}
  PRIVATE
  ${XOPENGL_SOURCE_DIR}/Inc
  ${XOPENGL_SOURCE_DIR}/glm
  ${SDL2_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../Render/Src
)

target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY})
target_link_libraries(${PROJECT_NAME} Render Engine Core)

# UPACKAGE_NAME drives the static-build package-symbol mangling (THIS_PACKAGE / GLoaded<Pkg>); the
# token must stay identical to IMPLEMENT_PACKAGE(XOpenGLDrv), the .so basename, the .int, and the ini
# RenderDevice=XOpenGLDrv.XOpenGLRenderDevice.
#
# SDLBUILD/SDL2BUILD: the fork's XOpenGLDrv.h picks its SDL build flavor from `__LINUX__`/`MACOSX`,
# which this engine does not define — so force SDL2 here (the header's auto-detect block then no-ops).
target_compile_definitions(${PROJECT_NAME} PRIVATE
  XOPENGLDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME}
  SDLBUILD=1 SDL2BUILD=1 SDL3BUILD=0
  # This engine reports ENGINE_VERSION 436 (for v436 script/mod compat) but its render layer is
  # still vanilla v400. The fetched fork gates its render-header include and v400 adaptations on
  # ENGINE_VERSION==400, so pin this driver's TUs to 400 (UnObjVer.h now #ifndef-guards the macro).
  # Safe: the render structs XOpenGLDrv touches are not ENGINE_VERSION-gated, so no ABI skew vs the
  # 436-compiled engine it links against.
  ENGINE_VERSION=400)

# Texture transcoding: on a GL context without S3TC, XOpenGL transcodes the DXT1 mips at upload, to ASTC 4x4
# (ARM astc-encoder) where the context has ASTC, else to ETC2 (etcpak); with neither it keeps the P8 mips. The
# choice is made from the context's formats at run time, so both encoders are built on every target. Only their
# codec sources are compiled (neither project's own CMakeLists.txt is used); each picks its SIMD paths from the
# compiler's target macros. Both are pinned to commits.
set(ETCPAK_GIT_REPOSITORY "https://github.com/wolfpld/etcpak.git" CACHE STRING "etcpak git repo")
set(ETCPAK_GIT_TAG "3d716e1550023dc5ff8b602636af8b76584e0b66" CACHE STRING "etcpak commit")
set(ASTCENC_GIT_REPOSITORY "https://github.com/ARM-software/astc-encoder.git" CACHE STRING "astc-encoder git repo")
set(ASTCENC_GIT_TAG "baff485b0ff36d2f95d28961605106502c653966" CACHE STRING "astc-encoder commit (5.7.0)")
include(FetchContent)
FetchContent_Declare(etcpak
  GIT_REPOSITORY "${ETCPAK_GIT_REPOSITORY}" GIT_TAG "${ETCPAK_GIT_TAG}" SOURCE_SUBDIR no-cmake-build)
FetchContent_Declare(astcenc
  GIT_REPOSITORY "${ASTCENC_GIT_REPOSITORY}" GIT_TAG "${ASTCENC_GIT_TAG}" SOURCE_SUBDIR no-cmake-build)
FetchContent_MakeAvailable(etcpak astcenc)
file(GLOB ASTCENC_SOURCES "${astcenc_SOURCE_DIR}/Source/astcenc_*.cpp")
target_sources(${PROJECT_NAME} PRIVATE
  ${etcpak_SOURCE_DIR}/ProcessRGB.cpp ${etcpak_SOURCE_DIR}/Tables.cpp ${etcpak_SOURCE_DIR}/Dither.cpp
  ${ASTCENC_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${etcpak_SOURCE_DIR} ${astcenc_SOURCE_DIR}/Source)
target_compile_definitions(${PROJECT_NAME} PRIVATE XOPENGL_TRANSCODE=1)
