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
  SDLBUILD=1 SDL2BUILD=1 SDL3BUILD=0)
