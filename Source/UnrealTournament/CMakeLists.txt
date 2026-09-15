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

# ufront 2.34 — tell the launcher which OPTIONAL audio subsystem this build contains, so the Windows ini
# normalisation can point at the REAL one instead of the silent null fallback. Keyed off the CMake option,
# so no other target's preprocessor output moves.
if(BUILD_NOPENALDRV)
  target_compile_definitions(${PROJECT_NAME} PRIVATE UT_HAS_NOPENALDRV=1)
endif()

# ufront 2.34 — OpenAL Soft is the one library the Windows build loads rather than links (see
# cmake/OpenALMingw.cmake). Put it beside the .exe as part of the build, so the output directory IS the
# runnable set.
if(OPENAL_MINGW_RUNTIME_DLL)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENAL_MINGW_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${OPENAL_MINGW_RUNTIME_DLL_NAME}"
    COMMENT "Copying ${OPENAL_MINGW_RUNTIME_DLL_NAME} (OpenAL Soft) next to the executable")
endif()

# ufront 2.70 — native Linux: our own openal-soft (cmake/OpenALSoft.cmake NATIVE) is DT_NEEDED by NOpenALDrv,
# so the binary finds it through $ORIGIN and the build puts it there, soname and all. A run tree that takes
# the binary takes libopenal.so.1 with it; without the file the loader stops before main.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN AND NOT ANDROID AND TARGET OpenAL)
  set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:OpenAL>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_SONAME_FILE_NAME:OpenAL>"
    COMMENT "Copying $<TARGET_SONAME_FILE_NAME:OpenAL> (openal-soft, the ufront fork) next to the executable")
endif()

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
    # ufront 2.13.1 — WasmFS + the JS-implemented FS backend (ported from engine-ut2004, the pilot).
    #
    # -sWASMFS moves the VFS into wasm, which BREAKS FetchFS's old seam: it intercepted file access by
    # monkey-patching the JS FS.open (legal only because the legacy filesystem implements
    # __syscall_openat in JS), and under WasmFS a C open() never routes through it. The replacement is a
    # WasmFS JSImpl backend whose file ops are implemented in JS (vendor/glue/wasmfs-shim.js forwards
    # them to the host-injected Module.UE.fs.backend), which is both the on-demand hook and the way to
    # keep asset bytes in JS typed arrays instead of a std::vector inside linear memory.
    #
    # -sFORCE_FILESYSTEM (above) stays REQUIRED — WasmFS omits the JS FS.* API otherwise, and the host
    # glue still uses it for user data, mods and the file browser.
    #
    # The EXPORTED_FUNCTIONS entries let the HOST stand the backend up entirely from JS, so this block
    # plus the vendored shim is the whole per-engine cost — no C++ in any engine repo.
    # NB: no _webrtc_deferred_open in this list — the UE1 engines deliberately do not use the
    # deferred-connect pair (see WebRTCDrv.cpp); it is a ut2004 thing. Listing an undefined symbol is a
    # hard wasm-ld error: 'symbol exported via --export not found'.
    -sWASMFS
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_close','_ue_exec_command','_wasmfs_create_jsimpl_backend','_wasmfs_create_file','_ufront_wasmfs_register','_ufront_wasmfs_set_pending']"
    # PThread (2.13.2): the host needs the worker list to hand the FS sync channel to the thread that runs
    # main(). Runtime methods are NOT on Module unless exported, so omitting it makes the host silently
    # no-op and the engine dies on its first asset read with no clue as to why.
    "-sEXPORTED_RUNTIME_METHODS=['callMain','stringToUTF8','UTF8ToString','HEAPU8','stackSave','stackAlloc','stackRestore','lengthBytesUTF8','PThread']"
    # The worker-side bootstrap in wasmfs-shim.js hangs off a __postset, and a postset is only EMITTED if
    # its $symbol is referenced. Nothing references it (it is pure side effect), so without this it is
    # dead-stripped SILENTLY — no error, no warning, and the engine dies on its first asset read.
    "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=['$ueWorkerBoot']"
    # ufront 2.13.2 — run the engine on a WORKER (threaded-only, decision D2).
    #
    # WHY: synchronous per-file restore-on-read from OPFS (2.13.4, the actual point of stage 2.13) needs
    # FileSystemSyncAccessHandle, which browsers expose ONLY on a worker; and a thread blocking on
    # Atomics.wait cannot do so on the browser main thread. So main() has to leave the main thread.
    #
    # -sOFFSCREENCANVASES_TO_PTHREAD transfers #canvas to that worker, which is what makes the direct
    # emscripten_webgl_create_context in NSDLViewport.cpp legal there. SDL2 keeps window + input; only
    # GL-context creation and the frame present are taken off SDL (its GL path is EGL, and Emscripten
    # proxies EGL to a main thread that can no longer see the transferred canvas — Emscripten #24792).
    # Both web render devices follow: XOpenGL adopts the current context (v400 already does) and UTGLR
    # points gl4es at emscripten's resolver instead of SDL's.
    #
    # -sASYNCIFY deliberately STAYS this step: dropping it is 2.13.3, after the worker move is proven.
    -pthread
    -sPROXY_TO_PTHREAD
    -sOFFSCREENCANVAS_SUPPORT
    # QUOTED: CMake treats an unquoted '#' as a comment, which silently truncates this to
    # "OFFSCREENCANVASES_TO_PTHREAD=" and fails the link with "string index out of range".
    "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"
    # ★ ufront 2.43d — WITHOUT THIS, OUR PRESENT CALL IS A STUB.
    #
    # NSDLViewport creates the context with explicitSwapControl=1 + renderViaOffscreenBackBuffer=1 and
    # then presents each frame with emscripten_webgl_commit_frame(). Emscripten implements BOTH of those
    # attributes only under -sOFFSCREEN_FRAMEBUFFER, which was never linked — so the shipped loader reads:
    #
    #   var _emscripten_webgl_do_commit_frame = () => { ...checks...; return 0 };   // that is all of it
    #
    # and blitOffscreenFramebuffer / defaultFbo occur ZERO times in SDLLaunch.js. So the engine has been
    # calling a no-op 80 times a second and presentation fell back entirely to Chromium's implicit
    # "the worker's task ended" propagation for a transferred OffscreenCanvas.
    #
    # That implicit path works on Linux and does NOT work on Windows/D3D11: measured on the owner's
    # machine with nothing attached, the engine produced 79.99 fps with a worst frame of 45 ms and
    # judder 3.4% — i.e. flawless — while the panel showed ~1 fps, and a DevTools trace of the same
    # session recorded UpdateLayer = 0/s with the main thread committing 5 frames/s. Only a CDP
    # screencast (which forces a real capture of the tab) ever made it smooth.
    #
    # ⚠ This is the cost 2.17 §C flagged when it found renderViaOffscreenBackBuffer being ignored: an
    # explicit blit per frame. That warning assumed the implicit path was working and the blit was pure
    # overhead. It is not working, so the trade is a per-frame blit against showing 1 frame in 80.
    -sOFFSCREEN_FRAMEBUFFER
    # main() itself is one of these. ufront 2.61: openal-soft adds TWO — the web backend's mixer thread and
    # the event thread every context starts — so 2 -> 4. A thread created with the pool empty still starts,
    # but only once the main thread has spun up a worker, which the audio ring would absorb as an underrun.
    -sPTHREAD_POOL_SIZE=4
    # ufront 2.61: openal-soft reports every AL error by throwing and catching it, so catching must be ON
    # (the first AL error would otherwise abort). Only its objects are -fexceptions; see Source/CMakeLists.txt.
    -sNO_DISABLE_EXCEPTION_CATCHING
    # The thin webrtc_* shim (defines the ABI's C-imported symbols, forwarding to host-injected
    # Module.UE.net.*). Heavy glue is NOT baked. Dead-stripped until the WebRTC net driver lands (2.7d).
    "SHELL:--js-library ${CMAKE_SOURCE_DIR}/../vendor/glue/webrtc-shim.js"
    "SHELL:--js-library ${CMAKE_SOURCE_DIR}/../vendor/glue/wasmfs-shim.js"
  )
  if(UT_WEB_PERF)
    # ufront 2.57 -- --emit-symbol-map costs the SHIPPED build NOTHING: the map is a SIDE FILE
    # (SDLLaunch.js.symbols), so the wasm is byte-for-byte the size it is without the flag and the code
    # section is untouched. It is what makes a PRODUCTION crash address readable -- the shipped wasm plus
    # this map resolves an offset to a function name offline, with no name section and no DWARF on the
    # wire. Measured on engine-unreal1: 3,545,604 bytes either way, and 0x2e0000 resolved to
    # FControlChannel::ReceivedBunch both from the map and from a name section built for the comparison.
    # DWARF is the one that cannot ride along: it forces "limited binaryen optimizations", which is why
    # the dev build's code section is more than TWICE this one's. Line numbers stay a debug-build job.
    # !! The map names every private engine symbol, so it must never reach the published tree.
    target_link_options(${PROJECT_NAME} PRIVATE -O3 -g0 -sASSERTIONS=0 --emit-symbol-map)
  else()
    target_link_options(${PROJECT_NAME} PRIVATE -g -sASSERTIONS=1)
  endif()
  # Preload the boot config so appInit has it before FetchFS streams the rest. UT99 v400's appInit
  # generates UnrealTournament.ini/User.ini from Default.ini + DefUser.ini, so both must exist at boot.
  # These come from the TRACKED, WEB-SPECIFIC set in this repo (Config/System/web) — NOT from the
  # out-of-tree asset dir. A game install's own inis are a native config and carry none of the web-only
  # settings (the [WebRTCDrv.WebRTCNetDriver] section, the GL/ES render device); sourcing config from an
  # untracked tree meant rebuilding the asset image silently reverted it. Config/System/*.ini (no subdir)
  # stays the platform-agnostic / Dreamcast-heritage reference set.
  # The derived UnrealTournament.ini/User.ini are excluded from the ufront asset manifest, so a stale
  # install copy can never shadow what appInit derives from these.
  foreach(_ini Default.ini DefUser.ini)
    if(EXISTS "${CMAKE_SOURCE_DIR}/../Config/System/web/${_ini}")
      set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS
        " --preload-file ${CMAKE_SOURCE_DIR}/../Config/System/web/${_ini}@/System/${_ini}")
    endif()
  endforeach()
  # The preloaded inis are INPUTS to the link: without LINK_DEPENDS, editing the web config leaves a
  # stale .data baked (cmake sees no source change and skips the relink) — a silent trap.
  set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/../Config/System/web/Default.ini"
    "${CMAKE_SOURCE_DIR}/../Config/System/web/DefUser.ini")
  # The --js-library shims are link INPUTS too, and CMake does not infer that from a link option: editing
  # a shim left a STALE .js baked while the build reported success, so the change appeared to have no
  # effect (2.13.2: the worker never received the target descriptor and silently cached nothing). Same
  # class of trap as the preloaded inis above, same fix.
  set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/../vendor/glue/webrtc-shim.js"
    "${CMAKE_SOURCE_DIR}/../vendor/glue/wasmfs-shim.js")
  # Bare loader: SDLLaunch.js (+ .wasm/.data), no .html, into build-<cfg>/SDLLaunch/. `SDLLaunch` is the
  # frozen cross-engine served-loader basename the monorepo host + packages/build assume (boot.ts loads
  # `SDLLaunch.js`; engine-unreal1/engine-ut2004 use it too) — engine differences are DATA, so the generic
  # frontend must not special-case ut99. (Our launcher source is Launch.cpp; only the artifact name conforms.)
  set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME SDLLaunch
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/SDLLaunch)

  # ufront 2.72 — 24-bit depth for the emulated default framebuffer: -sOFFSCREEN_FRAMEBUFFER's framebuffer is
  # allocated with GL_DEPTH_COMPONENT16 by Emscripten regardless of the context attributes (z-fighting in bands
  # on NOpenGLESDrv). offscreen-depth24.sh rewrites the emitted loader to D24S8 and asserts the rewrite count,
  # so a toolchain change fails the build rather than shipping 16-bit depth again. POST_BUILD: it re-runs on
  # every relink, and packages/build's publisher forces the relink (it deletes SDLLaunch.{js,wasm} first).
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/offscreen-depth24.sh" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/SDLLaunch.js"
    COMMENT "offscreen-depth24: D24S8 for the emulated default framebuffer")

  # Emit the artifact descriptor meta.json (docs/05 §4). abiVersion is read from the vendored frozen ABI
  # header (single source); provenance/toolchain are best-effort. packages/build validates it (schema +
  # abiVersion === glue ABI_VERSION + checksums + loaderStyle) before assembling serve-web.
  file(STRINGS ${CMAKE_SOURCE_DIR}/../vendor/glue/ue_abi.h _ue_abi_line REGEX "define[ \t]+UE_ABI_VERSION")
  string(REGEX MATCH "([0-9]+)" UE_ABI_VERSION "${_ue_abi_line}")
  execute_process(COMMAND git -C ${CMAKE_SOURCE_DIR}/.. rev-parse --short HEAD
    OUTPUT_VARIABLE META_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  string(TIMESTAMP META_BUILT_AT "%Y-%m-%dT%H:%M:%SZ" UTC)
  # ufront 2.70: the toolchain file knows where Emscripten is; `emcc --version` on PATH did not (a preset
  # build has only $EMSDK set), so every shipped descriptor said "unknown".
  set(META_EMCC "unknown")
  if(EXISTS "${EMSCRIPTEN_ROOT_PATH}/emscripten-version.txt")
    file(READ "${EMSCRIPTEN_ROOT_PATH}/emscripten-version.txt" _emcc_ver)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" META_EMCC "${_emcc_ver}")
  endif()
  # ufront 2.70: every fetched library as repo@branch|tag, from the cache variables the fetch itself read
  # (the web build always has NOpenALDrv, hence openal-soft and libxmp).
  include(${CMAKE_SOURCE_DIR}/cmake/ArtifactDeps.cmake)
  set(META_DEPS)
  ufront_artifact_dep(META_DEPS openal-soft OpenALSoft "${OPENALSOFT_GIT_REPOSITORY}" "${OPENALSOFT_GIT_TAG}")
  ufront_artifact_dep(META_DEPS libxmp libxmp "${LIBXMP_GIT_REPOSITORY}" "${LIBXMP_GIT_TAG}" "${LIBXMP_SOURCE_DIR}")
  if(BUILD_XOPENGLDRV)
    ufront_artifact_dep(META_DEPS xopengl XOpenGLDrv "${XOPENGL_GIT_REPOSITORY}" "${XOPENGL_GIT_TAG}")
  endif()
  if(BUILD_NOPENGLDRV)
    ufront_artifact_dep(META_DEPS utglr utglr "${UTGLR_GIT_REPOSITORY}" "${UTGLR_GIT_TAG}")
    ufront_artifact_dep(META_DEPS gl4es GL4ES "${GL4ES_GIT_REPOSITORY}" "${GL4ES_GIT_TAG}" "${GL4ES_SOURCE_DIR}")
  endif()
  find_program(NODE_EXECUTABLE node REQUIRED)
  # ufront 2.13.2 — the emitter is an INPUT to the artifact, and CMake cannot infer that from a
  # POST_BUILD COMMAND: editing it changes nothing the build watches, so the target does not relink,
  # POST_BUILD never re-runs, and a STALE meta.json ships while the build reports success. Hit for real
  # when buildFlags was corrected. Same class of trap as the preloaded config and the vendored shims,
  # same fix — LINK_DEPENDS forces the relink that re-runs POST_BUILD.
  set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/emit-artifact-meta.mjs")
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/emit-artifact-meta.mjs
      --out=$<TARGET_FILE_DIR:${PROJECT_NAME}>
      --target=ut99 --abi=${UE_ABI_VERSION}
      --gitSha=${META_GIT_SHA} --builtAt=${META_BUILT_AT} --emcc=${META_EMCC}
      ${META_DEPS}
    COMMENT "Emitting artifact meta.json (docs/05 §4)"
    VERBATIM
  )
endif()
