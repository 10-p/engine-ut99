project(NOpenGLESDrv C CXX)

if(NOT USE_SDL)
  message(FATAL_ERROR "NOpenGLESDrv requires NSDLDrv to function.")
endif()

set(SRC_FILES "NOpenGLESDrv.cpp")
# ufront 2.19: the vendored glad is NATIVE-only. On the web Emscripten links the GLES2 entry points in
# directly (-sFULL_ES2), and compiling glad here would collide symbol-for-symbol with XOpenGLDrv's own
# vendored glad in the single static binary — the reason the web build used to ship one or the other.
if(NOT EMSCRIPTEN)
  list(APPEND SRC_FILES "${CMAKE_SOURCE_DIR}/../Thirdparty/glad_es/glad.c")
endif()

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE
  ${CMAKE_SOURCE_DIR}/../Thirdparty/glad_es
  ${CMAKE_SOURCE_DIR}/../Thirdparty
  ${SDL2_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../Render/Src
)

target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY})

target_link_libraries(${PROJECT_NAME} Render Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE NOPENGLESDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
