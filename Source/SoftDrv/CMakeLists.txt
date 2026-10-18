#=============================================================================
# SoftDrv - Epic's v400 software render device (CPU rasterizer), built with -DASM=0 through the portable
# #if !ASM paths of its x86 __asm rasterization. Selected with -SOFTWARERENDERER.
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
  # RenderPrivate.h / UnRender.h come from ../Render/Src.
  ${CMAKE_CURRENT_SOURCE_DIR}/../Render/Src
)

target_link_libraries(${PROJECT_NAME} Render Engine Core)

# ASM=0: use the portable (non-x86) rasterization paths + the portable Pentium/no-MMX path (this v400 tree
# already has the #if !ASM fallbacks; GIsMMX is force-disabled on x86 in SoftDrvPrivate.h).
target_compile_definitions(${PROJECT_NAME} PRIVATE SOFTDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME} ASM=0)

# The SizeX/SizeY-indexed scratch tables are static arrays, so their bound costs .bss at every resolution;
# a viewport above it degrades rather than corrupting (Src/SoftDrvPrivate.h). The wasm presets raise it to
# 8192: the web canvas is the display's device-pixel size, and the resize path bypasses SetRes's clamp.
set(SOFTDRV_MAX_SCREEN "4096" CACHE STRING "Max software-renderer viewport dimension (static table bound)")
target_compile_definitions(${PROJECT_NAME} PRIVATE SOFTDRV_MAX_SCREEN=${SOFTDRV_MAX_SCREEN})
