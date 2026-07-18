project(SoundDrv CXX)

# ufront step 2.7a (engine-ut99): the null (silent) audio subsystem the shipped
# inis reference (SoundDrv.NullAudioSubsystem). Mirrors the ut99dc driver CMake
# pattern (cf. AICADrv/CMakeLists.txt); flat layout (Null.cpp).

set(SRC_FILES
  "Null.cpp"
)

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE SOUNDDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})
