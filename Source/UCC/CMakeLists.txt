project(UCC CXX)

# ufront 2.12 Part B: ucc — the UT99 command-line tool (dedicated server / UnrealScript `make` / commandlets).
# A headless console EXECUTABLE (no client, no SDL, no renderer): UCC.cpp sets GIsClient off and dispatches
# UCommandlet subclasses from argv. Native-only (there is no ucc on the web build). Was previously an
# `add_library` with a stray RENDER_EXPORTS def (why add_subdirectory(UCC) was commented out) — fixed here.
set(SRC_FILES
  "Src/UCC.cpp"
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

# Retail parity: the binary is `ucc-bin` (classic Linux UT ships a `ucc` wrapper script over ucc-bin).
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ucc-bin)

target_include_directories(${PROJECT_NAME}
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

# Headless: Engine + Core only. Editor (make), IpDrv (server), game packages load dynamically at runtime.
target_link_libraries(${PROJECT_NAME} Engine Core)

# UCC.cpp is retail-era code: its file-manager #if uses the legacy uppercase __LINUX__ (the modernized
# Launch.cpp includes FFileManagerLinux directly). Define it so ucc picks FFileManagerLinux + FMallocAnsi.
target_compile_definitions(${PROJECT_NAME} PRIVATE UPACKAGE_NAME=${PROJECT_NAME} __LINUX__)
