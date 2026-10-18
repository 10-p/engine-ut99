project(UWeb CXX)

# UWeb's natives. The game ships UWeb.u, whose native classes must bind when anything loads it (the multiplayer
# start dialog does); without them that is a fatal "Can't bind to native class UWeb.WebRequest". Building UWeb
# starts no web server: UWeb.WebServer listens only when a ServerActor spawns it.
set(SRC_FILES
  "Src/UWeb.cpp"
  "Src/WebServer.cpp"
)

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

# The natives include only Engine.h.
target_link_libraries(${PROJECT_NAME} Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE UWEB_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
