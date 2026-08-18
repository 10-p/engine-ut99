project(UWeb)

# ufront 2.38 — UWeb was never compiled, and the game shipped its SCRIPT half anyway.
#
# ★ Retail UT99 ships UWeb.u (script, with `native` classes) next to UWeb.dll (their natives). This repo
# has always carried the sources — Src/UWeb.cpp + Src/WebServer.cpp — and never built them, so the moment
# anything loaded UWeb.u the engine hit:
#
#     Critical: Can't bind to native class UWeb.WebRequest
#     Exiting due to error
#
# which is a HARD appError, not a soft miss. That is the crash the owner reported on "Start New
# Multiplayer Game": the multiplayer start dialog reaches the server-admin options, those pull in UWeb,
# and the game died before the dialog could appear.
#
# Building it costs two small translation units and makes the shipped package whole. ⚠ Compiling UWeb does
# NOT start a web server: UWeb.WebServer is an actor that only listens if something spawns it (a
# ServerActor), and the web launcher clears ServerActors. It is the CLASS BINDING that was missing.
set(SRC_FILES
  "Src/UWeb.cpp"
  "Src/WebServer.cpp"
)

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

# UWeb's natives are TcpLink-derived, so it needs IpDrv's headers as well as Engine/Core.
target_link_libraries(${PROJECT_NAME} IpDrv Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE UWEB_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
