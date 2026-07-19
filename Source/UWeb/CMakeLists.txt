project(UWeb CXX)

# v436-compat port: UWeb was present in-tree but never wired into the build. JailBreak (and
# UTServerAdmin) reference its native classes (WebRequest/WebResponse/WebApplication/WebServer),
# so a stock run has no UWeb.so and UClass::Bind aborts ("Can't bind to native class
# UWeb.WebRequest"). Build it like the other leaf packages.
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

target_link_libraries(${PROJECT_NAME} Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE UWEB_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
