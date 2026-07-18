project(XOpenGLDrv C CXX)

# ufront step 2.7 (engine-ut99): XOpenGLDrv — the full OldUnreal OpenGL renderer, ported to vanilla
# UT99 v400 (UE1). All v400 adaptations in Src/Inc are gated behind `#if ENGINE_VERSION==400` so the
# same tree can later target Unreal v200. See the repo-root plan + ENGINE_CHANGES.md.
#
# Like NOpenGLESDrv, this device does NOT create its own GL context on v400 — it reuses the context
# NSDLViewport already makes current (see Src/XOpenGL.cpp Init, gated). So it needs SDL/NSDLDrv.
if(NOT USE_SDL)
  message(FATAL_ERROR "XOpenGLDrv requires NSDLDrv (SDL) to function.")
endif()

# All Src/*.cpp are standalone translation units (incl. the *_GLSL.cpp shader files and the vendored
# desktop-GL glad.cpp loader).
file(GLOB SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cpp")

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

# Isolated include set: the driver vendors its OWN desktop-GL `glad.h`/`glext.h` (Inc/) and its own
# glm (glm/) — deliberately NOT the engine's Thirdparty/glad_es (GLES loader) or trimmed Thirdparty/glm
# (missing gtc/), to avoid header/symbol clashes with NOpenGLESDrv. Render private headers
# (RenderPrivate.h/UnRender.h) come from ../Render/Src, exactly as NOpenGLESDrv does.
target_include_directories(${PROJECT_NAME}
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  ${CMAKE_CURRENT_SOURCE_DIR}/glm
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
