# OpenGLDrv — UTGLR (Chris Dohnal's legacy-OpenGL UE1 render device) ported to engine-ut99.
#
# Driver-only port (see PORTING.md). Package/class name is OpenGLDrv.OpenGLRenderDevice — the
# faithful upstream name, which also makes NSDLDrv create the right context (its GL-window test
# keys on the substring "OPENGL"; "GLES" is absent so native gets a desktop-GL compat context).
#
# Backends:
#   default        -> resolves GL entry points at runtime via SDL_GL_GetProcAddress against the
#                     desktop-GL context NSDLDrv creates (the native "compat oracle"). No -lGL:
#                     every gl* call goes through UTGLR's own function-pointer table.
#   USE_GL4ES=ON   -> link the gl4es static lib (desktop GL -> GLES2) for the WebGL path. The
#                     gl4es::GL alias + FetchContent live in the top-level Source/CMakeLists.txt.

project(OpenGLDrv C CXX)

if(NOT USE_SDL)
  message(FATAL_ERROR "OpenGLDrv (UTGLR) requires NSDLDrv/SDL2 to function.")
endif()

set(SRC_FILES "OpenGL.cpp" "OpenGLDrv.cpp" "c_gclip.cpp")

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE
  ${SDL2_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../Render/Src
)

target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY})
target_link_libraries(${PROJECT_NAME} Render Engine Core)

# UTGLR build config + this engine's (ut99dc / UE1 v400) portability gate + the Unix/SDL path.
# UTGLR_ENGINE_UT99DC guards every engine-specific deviation from upstream UTGLR (grep it).
target_compile_definitions(${PROJECT_NAME} PRIVATE
  UPACKAGE_NAME=${PROJECT_NAME}
  UTGLR_UT_BUILD
  UTGLR_ENGINE_UT99DC
  __UNIX__
)

# gl4es backend (M3 / WebGL path). Inert unless USE_GL4ES is ON and the gl4es::GL target exists
# (created by the top-level FetchContent block). When active, gl4es supplies <GL/gl.h> and the GL
# symbols instead of the system OpenGL (Pitfall: never also link system -lGL).
if(USE_GL4ES)
  if(NOT TARGET gl4es::GL)
    message(FATAL_ERROR "OpenGLDrv: USE_GL4ES is ON but the gl4es::GL target is missing "
                        "(top-level FetchContent not configured).")
  endif()
  target_include_directories(${PROJECT_NAME} BEFORE PRIVATE ${gl4es_SOURCE_DIR}/include)
  target_link_libraries(${PROJECT_NAME} gl4es::GL)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UTGLR_USE_GL4ES)
endif()
