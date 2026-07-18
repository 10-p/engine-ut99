#=============================================================================
# NullDrv - null render device (GPU-free). A no-op URenderDevice: no GL context, no rasterization, draws
# nothing. Selected via -NULLRENDERER for a headless / "poor-man's dedicated server" run (host listens with
# an empty tab). Additive like SoftDrv; the NSDL viewport gives it a software SDL_Texture surface because the
# device name lacks "OPENGL". Mirrors UT2004's Engine.NullRenderDevice for the UE1 immediate-mode interface.
#=============================================================================

project(NullDrv CXX)

set(SRC_FILES
  "Src/NullDrv.cpp"
)

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  PRIVATE
  # RenderPrivate.h / UnRender.h (FSceneNode / FSurfaceInfo / FSpanBuffer decls) come from ../Render/Src,
  # exactly as SoftDrv / NOpenGLESDrv / XOpenGLDrv do.
  ${CMAKE_CURRENT_SOURCE_DIR}/../Render/Src
)

target_link_libraries(${PROJECT_NAME} Render Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE NULLDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
