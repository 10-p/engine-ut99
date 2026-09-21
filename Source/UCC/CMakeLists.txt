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

if(BUILD_STATIC)
  # ufront 2.79 — a STATIC build (the `native` and `windows` presets: UNREAL_STATIC) has no package .so files
  # to load at runtime: appGetDllHandle answers from the in-binary export registry (Core/UnStaticExports),
  # and a package is only in that registry if its objects are in THIS binary. Linking just Engine + Core
  # as ordinary archives pulled in only the objects UCC.cpp references, so not even Engine's own
  # registrants survived ("Loading: Package Engine" with no "Bound to Engine"), and every commandlet
  # outside Core was missing -- `ucc server` said "Commandlet server not found".
  #
  # So link the package set the way the game target does (UnrealTournament/CMakeLists.txt), under
  # --whole-archive so each package's GLoaded<Pkg> export and IMPLEMENT_CLASS registrants are kept -- minus
  # everything a headless tool never opens: the viewport (NSDLDrv/SDL2), the render devices and the audio
  # subsystems (no GIsClient in a dedicated server, so none of them is ever loaded; they would only drag
  # SDL2/GL/OpenAL into the binary). Same shape as engine-ut2004's UCC.
  #   Render  -- the software scene renderer; no external libraries, kept so client-flagged commandlets can load it.
  #   IpDrv   -- TcpNetDriver (the server's listen socket), UdpLink/TcpLink, the master/update commandlets.
  #   UWeb    -- the game ships UWeb.u, whose natives must bind when a server actor loads it.
  #   Fire    -- procedural textures referenced by the stock maps.
  #   Editor  -- UT99 content references Editor classes (bot meshes/skins), and it carries `ucc make`.
  set(UCC_LINK_PACKAGES Core Engine Render IpDrv UWeb Fire)
  if(BUILD_EDITOR)
    list(APPEND UCC_LINK_PACKAGES Editor)
  endif()
  target_link_libraries(${PROJECT_NAME} -Wl,--whole-archive ${UCC_LINK_PACKAGES} -Wl,--no-whole-archive)
else()
  # Shared build: Engine + Core only. Editor (make), IpDrv (server), game packages load dynamically at runtime.
  target_link_libraries(${PROJECT_NAME} Engine Core)
endif()

# UCC.cpp is retail-era code: its file-manager #if uses the legacy uppercase __LINUX__ (the modernized
# Launch.cpp includes FFileManagerLinux directly). Define it so ucc picks FFileManagerLinux + FMallocAnsi.
target_compile_definitions(${PROJECT_NAME} PRIVATE UPACKAGE_NAME=${PROJECT_NAME} __LINUX__)
