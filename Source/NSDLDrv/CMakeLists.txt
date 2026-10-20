project(NSDLDrv CXX)

set(SRC_FILES
  "Src/NSDLViewport.cpp"
  "Src/NSDLClient.cpp"
  "Src/NSDLDrv.cpp"
)
# The touch overlay's JNI side (the Gradle project's GameActivity calls it).
if(ANDROID)
  list(APPEND SRC_FILES "Src/NSDLAndroid.cpp")
endif()

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  ${SDL2_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY} Engine Core)
if(ANDROID)
  target_link_libraries(${PROJECT_NAME} log)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE NSDLDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
