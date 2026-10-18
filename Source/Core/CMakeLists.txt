project(Core CXX)

set(SRC_FILES
  "Src/UnProp.cpp"
  "Src/UnObj.cpp"
  "Src/UnCorSc.cpp"
  "Src/UnName.cpp"
  "Src/UnMem.cpp"
  "Src/UnMisc.cpp"
  "Src/UnMath.cpp"
  "Src/UnCoreNet.cpp"
  "Src/UnClass.cpp"
  "Src/UnScriptOverride.cpp"
  "Src/UnCache.cpp"
  "Src/UnBits.cpp"
  "Src/UnAnsi.cpp"
  "Src/Core.cpp"
  "Src/UFactory.cpp"
  "Src/UExporter.cpp"
)

# The platform layer is chosen by compiler, not by target OS: MSVC gets Epic's Win32 core (UnVcWin32.cpp,
# whose body sits inside `#if _MSC_VER`), every GCC/Clang target the portable UnUnix.cpp, and MinGW's few
# Win32-only calls live in UnMinGW.cpp beside it.
if(MSVC)
  list(APPEND SRC_FILES "Src/UnVcWin32.cpp")
else()
  # UnGUID/UnMinGW before UnUnix, as in the original list: source order is link order.
  if(TARGET_IS_WINDOWS)
    # UnGUID.cpp is the OSF/DCE POSIX implementation (gettimeofday + a MAC address); on Windows
    # UnMinGW.cpp answers appGetGUID with CoCreateGuid instead.
    list(APPEND SRC_FILES "Src/UnMinGW.cpp")
  else()
    list(APPEND SRC_FILES "Src/UnGUID.cpp")
  endif()
  list(APPEND SRC_FILES "Src/UnUnix.cpp")
endif()

if(BUILD_STATIC)
  list(APPEND SRC_FILES "Src/UnStaticExports.cpp")
endif()

if(TARGET_IS_DREAMCAST AND DREAMCAST_USE_FILE_POOL)
  list(APPEND SRC_FILES "Src/UnFilePool.cpp")
endif()

if(NOT MSVC AND NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
  # FIXME: the very funny reference juggling UE does with FFrame::Step() confuses gcc,
  # so we'll lower optimization level for the core UnrealScript interpreter for now
  set_source_files_properties("Src/UnCorSc.cpp" PROPERTIES COMPILE_FLAGS -Og)
  set_source_files_properties("Src/UnProp.cpp" PROPERTIES COMPILE_FLAGS -Og)
endif()

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)
# vendor/glue for the optional pkgguid ABI header, which UnMisc.cpp includes on the web only.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/../vendor/glue)

target_compile_definitions(${PROJECT_NAME} PRIVATE CORE_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})

if(USE_SDL)
  target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARY})
  target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIR})
endif()
