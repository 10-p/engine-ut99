#=============================================================================
# NullDrv - a render device that draws nothing and needs no GL context, selected with -NULLRENDERER for a
# headless run. The SDL viewport gives it a software surface, because the device name lacks "OPENGL".
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
