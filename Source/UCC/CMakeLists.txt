project(UCC CXX)

# ucc — the command-line tool (dedicated server, UnrealScript `make`, commandlets): a headless executable
# (no client, no SDL, no renderer) that dispatches UCommandlet subclasses from argv. Native only.
set(SRC_FILES
  "Src/UCC.cpp"
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

# Retail parity: the binary is `ucc-bin` (classic Linux UT ships a `ucc` wrapper script over ucc-bin), and `ucc.exe`
# on Windows, the name the game's own UCC.exe has and the menus' dedicated-server relaunch starts.
if(WIN32)
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ucc)
else()
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ucc-bin)
endif()

target_include_directories(${PROJECT_NAME}
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

if(BUILD_STATIC)
  # A static build (UNREAL_STATIC) loads no package libraries: appGetDllHandle answers from the in-binary export
  # registry (Core/UnStaticExports), which holds only the packages linked into this binary. So link the package
  # set as the game target does, under --whole-archive (each package's GLoaded<Pkg> export and IMPLEMENT_CLASS
  # registrants are kept), minus what a headless tool never opens: the viewport, render devices and audio.
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

# UCC.cpp's file-manager #if keys on __LINUX__: defined for every build here, so a non-WIN32 ucc takes
# FFileManagerLinux + FMallocAnsi.
target_compile_definitions(${PROJECT_NAME} PRIVATE UPACKAGE_NAME=${PROJECT_NAME} __LINUX__)
