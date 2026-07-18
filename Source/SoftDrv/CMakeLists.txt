#=============================================================================
# SoftDrv - software render device (CPU rasterizer). Ported into engine-ut99 from the original UT99 v400
# source (/home/ion/UT/SoftDrv); its x86 __asm rasterization already carries portable #if !ASM paths, so
# it builds with -DASM=0 like engine-unreal1's SoftDrv. Pure software (no glad/SDL GL), so it coexists with
# XOpenGLDrv on web (not subject to the NOpenGLES x XOpenGL glad-clash guard). Selected via -SOFTWARERENDERER.
#=============================================================================

project(SoftDrv CXX)

set(SRC_FILES
  "Src/SoftDrv.cpp"
  "Src/DrawPoly.cpp"
  "Src/DrawSurf.cpp"
  "Src/DrawTile.cpp"
  "Src/Line2D.cpp"
  "Src/Hit.cpp"
)

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  PRIVATE
  # RenderPrivate.h / UnRender.h come from ../Render/Src, exactly as NOpenGLESDrv / XOpenGLDrv do.
  ${CMAKE_CURRENT_SOURCE_DIR}/../Render/Src
)

target_link_libraries(${PROJECT_NAME} Render Engine Core)

# ASM=0: use the portable (non-x86) rasterization paths + the portable Pentium/no-MMX path (this v400 tree
# already has the #if !ASM fallbacks; GIsMMX is force-disabled on x86 in SoftDrvPrivate.h).
target_compile_definitions(${PROJECT_NAME} PRIVATE SOFTDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME} ASM=0)
