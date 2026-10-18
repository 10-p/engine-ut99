project(WebRTCDrv)

# The WebRTC net driver (browser multiplayer, Emscripten only): its transport is the glue ABI
# (vendor/glue), not BSD sockets.
set(SRC_FILES
  "WebRTCDrv.cpp"
  # The synthetic LAN that lets the stock server browser work over WebRTC. IpDrv's AUdpLink calls
  # into it (one-way; see WebRTCLan.h), so this package's root is a PUBLIC include dir below.
  "WebRTCLan.cpp"
)

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

target_link_libraries(${PROJECT_NAME} Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE WEBRTCDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
