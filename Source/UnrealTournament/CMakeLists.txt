project(UnrealTournament CXX)

set(SRC_FILES "Src/Launch.cpp")

if(TARGET_IS_DREAMCAST AND DREAMCAST_NO_WHOLE_ARCHIVE)
  list(APPEND SRC_FILES "Src/ForceLink.cpp")
endif()

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

target_compile_definitions(${PROJECT_NAME} PRIVATE UNREAL_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})

# Select the appropriate viewport / window driver
if(USE_SDL)
  # SDL / NSDLDrv path (POSIX/Windows with SDL2).
  # ufront 2.7b (web): under Emscripten, SDL2 comes from the -sUSE_SDL=2 port (link flags below) and NSDLDrv
  # is pulled in via the --whole-archive static set — linking it again here double-defines every NSDLDrv
  # symbol (GNU ld tolerates it; wasm-ld does not).
  if(NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY} NSDLDrv)
  endif()
  target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIR})
elseif(TARGET_IS_DREAMCAST)
  # Native Dreamcast driver (KOSDrv) – added from the top‑level CMakeLists
  target_link_libraries(${PROJECT_NAME} KOSDrv)
else()
  # Non‑Windows, non‑Dreamcast platforms currently only support the SDL path
  message(FATAL_ERROR "Platforms other than Windows and Dreamcast can only use NSDLDrv.")
endif()


if(BUILD_STATIC)
  set(LINK_PACKAGES ${INSTALL_TARGETS})
  list(REMOVE_ITEM LINK_PACKAGES ${PROJECT_NAME})
  if(BUILD_DCUTIL)
    list(REMOVE_ITEM LINK_PACKAGES DCUtil)
  endif()
  if(TARGET_IS_DREAMCAST AND DREAMCAST_NO_WHOLE_ARCHIVE)
    target_link_libraries(${PROJECT_NAME} ${LINK_PACKAGES})
  else()
    target_link_libraries(${PROJECT_NAME} -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive)
  endif()
else()
  target_link_libraries(${PROJECT_NAME} Engine Core)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE UNREAL_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})

if(TARGET_IS_DREAMCAST AND DREAMCAST_USE_FATFS)
  target_link_libraries(${PROJECT_NAME} fatfs)
endif()

# ufront step 2.7b (web port) — emit a BARE LOADER (Launch.{js,wasm,data}); no --shell-file. The monorepo
# owns the host page and injects the heavy glue (FetchFS/IDB/WebRTC) at runtime; only the thin webrtc_* shim
# is baked (vendor/glue/webrtc-shim.js). Renderer = NOpenGLESDrv (GLES2->WebGL) in the --whole-archive static
# set; SDL2 comes from the Emscripten port. Mirrors engine-unreal1's launcher EMSCRIPTEN block.
if(EMSCRIPTEN)
  set(UT99_ASSETS_DIR "/home/ion/ut99-assets" CACHE PATH
      "Retail UT99 v400 assets (System/Default.ini preloaded into MEMFS; the rest streams via FetchFS)")
  target_link_options(${PROJECT_NAME} PRIVATE
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=262144
    # emscripten_sleep = main-loop inversion (the live Asyncify use). __syscall_openat is listed for FetchFS
    # on-demand open (ABI-consistent with the siblings; the open is sync-XHR).
    "-sASYNCIFY_IMPORTS=['emscripten_sleep','emscripten_set_main_loop','__syscall_openat']"
    # ufront 2.9: one binary that creates EITHER a WebGL1 (ES2) or WebGL2 (ES3) context at runtime
    # (NSDLViewport picks the version by render-device name). MAX/MIN webgl version allows both;
    # GL_ENABLE_GET_PROC_ADDRESS lets glad (XOpenGL, ES3) + gl4es (UTGLR, ES2) resolve entry points.
    # FULL_ES2 is REQUIRED for UTGLR: gl4es emits client-side vertex arrays, which raw WebGL rejects
    # ("no ARRAY_BUFFER bound"); FULL_ES2 is emscripten's client-array emulation (temp VBOs). It coexists
    # with the WebGL2 context XOpenGL requests (XOpenGL uses real VBOs, unaffected).
    -sMAX_WEBGL_VERSION=2
    -sMIN_WEBGL_VERSION=1
    -sGL_ENABLE_GET_PROC_ADDRESS=1
    -sFULL_ES2=1
    -sUSE_SDL=2
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=268435456
    -sSTACK_SIZE=2097152
    -sFORCE_FILESYSTEM
    "-sEXPORTED_RUNTIME_METHODS=['callMain','stringToUTF8','UTF8ToString','HEAPU8','stackSave','stackAlloc','stackRestore']"
    # The thin webrtc_* shim (defines the ABI's C-imported symbols, forwarding to host-injected
    # Module.UE.net.*). Heavy glue is NOT baked. Dead-stripped until the WebRTC net driver lands (2.7d).
    "SHELL:--js-library ${CMAKE_SOURCE_DIR}/../vendor/glue/webrtc-shim.js"
  )
  if(UT_WEB_PERF)
    target_link_options(${PROJECT_NAME} PRIVATE -O3 -g0 -sASSERTIONS=0)
  else()
    target_link_options(${PROJECT_NAME} PRIVATE -g -sASSERTIONS=1)
  endif()
  # Preload the boot config so appInit has it before FetchFS streams the rest. UT99 v400's appInit
  # generates UnrealTournament.ini/User.ini from Default.ini + DefUser.ini, so both must exist at boot;
  # in the real host these come from FetchFS (preRun), but the standalone bare loader needs them baked.
  foreach(_ini Default.ini DefUser.ini)
    if(EXISTS "${UT99_ASSETS_DIR}/System/${_ini}")
      set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS
        " --preload-file ${UT99_ASSETS_DIR}/System/${_ini}@/System/${_ini}")
    endif()
  endforeach()
  # Bare loader: SDLLaunch.js (+ .wasm/.data), no .html, into build-<cfg>/SDLLaunch/. `SDLLaunch` is the
  # frozen cross-engine served-loader basename the monorepo host + packages/build assume (boot.ts loads
  # `SDLLaunch.js`; engine-unreal1/engine-ut2004 use it too) — engine differences are DATA, so the generic
  # frontend must not special-case ut99. (Our launcher source is Launch.cpp; only the artifact name conforms.)
  set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME SDLLaunch
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/SDLLaunch)

  # Emit the artifact descriptor meta.json (docs/05 §4). abiVersion is read from the vendored frozen ABI
  # header (single source); provenance/toolchain are best-effort. packages/build validates it (schema +
  # abiVersion === glue ABI_VERSION + checksums + loaderStyle) before assembling serve-web.
  file(STRINGS ${CMAKE_SOURCE_DIR}/../vendor/glue/ue_abi.h _ue_abi_line REGEX "define[ \t]+UE_ABI_VERSION")
  string(REGEX MATCH "([0-9]+)" UE_ABI_VERSION "${_ue_abi_line}")
  execute_process(COMMAND git -C ${CMAKE_SOURCE_DIR}/.. rev-parse --short HEAD
    OUTPUT_VARIABLE META_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  string(TIMESTAMP META_BUILT_AT "%Y-%m-%dT%H:%M:%SZ" UTC)
  execute_process(COMMAND emcc --version
    OUTPUT_VARIABLE _emcc_ver OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" META_EMCC "${_emcc_ver}")
  find_program(NODE_EXECUTABLE node REQUIRED)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/emit-artifact-meta.mjs
      --out=$<TARGET_FILE_DIR:${PROJECT_NAME}>
      --target=ut99 --abi=${UE_ABI_VERSION}
      --gitSha=${META_GIT_SHA} --builtAt=${META_BUILT_AT} --emcc=${META_EMCC}
    COMMENT "Emitting artifact meta.json (docs/05 §4)"
    VERBATIM
  )
endif()
