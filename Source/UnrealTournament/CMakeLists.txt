project(UnrealTournament CXX)

set(SRC_FILES "Src/Launch.cpp")

if(TARGET_IS_DREAMCAST AND DREAMCAST_NO_WHOLE_ARCHIVE)
  list(APPEND SRC_FILES "Src/ForceLink.cpp")
endif()

# Android: the game is a shared library, libUT99.so, that SDLActivity loads (GameActivity.getLibraries names
# it) and whose SDL_main it calls; everywhere else an executable.
if(UE_ANDROID_CLIENT)
  add_library(${PROJECT_NAME} SHARED ${SRC_FILES})
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME UT99 PREFIX "lib" SUFFIX ".so")
  target_link_libraries(${PROJECT_NAME} log android)
else()
  add_executable(${PROJECT_NAME} ${SRC_FILES})
endif()

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
  # Under Emscripten SDL2 is the -sUSE_SDL=2 port and NSDLDrv comes in with the --whole-archive static set;
  # linking it again would define every NSDLDrv symbol twice, which wasm-ld rejects.
  # On Android NSDLDrv comes in with the --whole-archive set too, and lld rejects the second copy.
  if(UE_ANDROID_CLIENT)
    target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY})
  elseif(NOT EMSCRIPTEN)
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

# Windows: OpenAL32.dll (cmake/OpenALMingw.cmake) goes beside the .exe, so the output directory is the
# runnable set.
if(OPENAL_MINGW_RUNTIME_DLL)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENAL_MINGW_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${OPENAL_MINGW_RUNTIME_DLL_NAME}"
    COMMENT "Copying ${OPENAL_MINGW_RUNTIME_DLL_NAME} (OpenAL Soft) next to the executable")
endif()

# Native Linux: NOpenALDrv needs the openal-soft fork's shared library (cmake/OpenALSoft.cmake); the binary
# finds it through $ORIGIN and the build copies it there under its soname.
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

# The web build emits a bare loader (SDLLaunch.{js,wasm,data}, no --shell-file). The frontend owns the host
# page and injects the heavy glue at runtime; only the thin shims in vendor/glue are linked. Every render
# device is in the --whole-archive static set; SDL2 is the Emscripten port.
if(EMSCRIPTEN)
  target_link_options(${PROJECT_NAME} PRIVATE
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=262144
    # emscripten_sleep is the main loop's yield; __syscall_openat lets FetchFS open a file on demand.
    "-sASYNCIFY_IMPORTS=['emscripten_sleep','emscripten_set_main_loop','__syscall_openat']"
    # One binary that creates a WebGL 2 context, or WebGL 1 with -WEBGL=1 or as a fallback (NSDLViewport).
    # GL_ENABLE_GET_PROC_ADDRESS lets glad (XOpenGL) and gl4es (UTGLR) resolve entry points. FULL_ES2 is required
    # by UTGLR: gl4es emits client-side vertex arrays, which WebGL rejects, and FULL_ES2 emulates them.
    -sMAX_WEBGL_VERSION=2
    -sMIN_WEBGL_VERSION=1
    -sGL_ENABLE_GET_PROC_ADDRESS=1
    -sFULL_ES2=1
    -sUSE_SDL=2
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=268435456
    -sSTACK_SIZE=2097152
    -sFORCE_FILESYSTEM
    # WasmFS with a JS-implemented backend: vendor/glue/wasmfs-shim.js forwards the file ops to the host's
    # Module.UE.fs.backend, which opens assets on demand and keeps their bytes in JS. -sFORCE_FILESYSTEM is still
    # required: the host uses the JS FS.* API for user data, mods and the file browser. The EXPORTED_FUNCTIONS
    # entries let the host set the backend up from JS.
    -sWASMFS
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_close','_ue_exec_command','_wasmfs_create_jsimpl_backend','_wasmfs_create_file','_ufront_wasmfs_register','_ufront_wasmfs_set_pending']"
    # PThread: the host needs the worker list to hand the FS sync channel to the thread that runs main().
    "-sEXPORTED_RUNTIME_METHODS=['callMain','stringToUTF8','UTF8ToString','HEAPU8','stackSave','stackAlloc','stackRestore','lengthBytesUTF8','PThread']"
    # The worker-side bootstrap in wasmfs-shim.js hangs off a __postset, which is emitted only when its $symbol
    # is referenced; nothing references it, so it is listed here.
    "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=['$ueWorkerBoot']"
    # main() runs on a worker: synchronous reads from OPFS need FileSystemSyncAccessHandle and a blocking
    # Atomics.wait, which browsers allow only off the main thread. #canvas is transferred to that worker, where
    # NSDLViewport creates the WebGL context itself and presents each frame; SDL2 keeps the window and input (its
    # EGL path is proxied to the main thread, which can no longer see the canvas — Emscripten #24792).
    -pthread
    -sPROXY_TO_PTHREAD
    -sOFFSCREENCANVAS_SUPPORT
    # Quoted: an unquoted '#' starts a CMake comment.
    "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"
    # NSDLViewport creates the context with explicitSwapControl and renderViaOffscreenBackBuffer and presents
    # each frame with emscripten_webgl_commit_frame(); Emscripten implements both only under
    # -sOFFSCREEN_FRAMEBUFFER (without it the commit is a no-op and the browser may never show the frame).
    -sOFFSCREEN_FRAMEBUFFER
    # main() is one of these; openal-soft adds its web backend's mixer thread and every context's event thread.
    # A thread created with the pool empty starts only once the main thread has spun up a worker.
    -sPTHREAD_POOL_SIZE=4
    # openal-soft reports AL errors by throwing and catching them, so catching is linked in; only its objects
    # are compiled with -fexceptions (Source/CMakeLists.txt).
    -sNO_DISABLE_EXCEPTION_CATCHING
    # The thin net shim: defines the ABI's ue_net_* imports and forwards them to the host's Module.UE.net.
    "SHELL:--js-library ${CMAKE_SOURCE_DIR}/../vendor/glue/net-shim.js"
    "SHELL:--js-library ${CMAKE_SOURCE_DIR}/../vendor/glue/wasmfs-shim.js"
    # The optional pkgguid module: a package's GUID from the page's manifest (Core/Src/UnMisc.cpp).
    "SHELL:--js-library ${CMAKE_SOURCE_DIR}/../vendor/glue/pkgguid-shim.js"
    # The multisampled emulated back buffer (ufront_backbuffer_samples) for the render devices that draw into
    # framebuffer 0 (NOpenGLESDrv, OpenGLDrv/UTGLR).
    "SHELL:--js-library ${CMAKE_CURRENT_SOURCE_DIR}/msaa-backbuffer.js"
  )
  if(UE_WEB_PERF)
    # --emit-symbol-map writes a side file (SDLLaunch.js.symbols) and leaves the wasm unchanged; it resolves a
    # shipped crash address to a function name offline. The map names every private symbol, so it is never
    # published.
    target_link_options(${PROJECT_NAME} PRIVATE -O3 -g0 -sASSERTIONS=0 --emit-symbol-map)
  else()
    target_link_options(${PROJECT_NAME} PRIVATE -g -sASSERTIONS=1)
  endif()
  # Preload the web inis (Config/System/web) into /System: appInit derives UnrealTournament.ini and User.ini
  # from Default.ini and DefUser.ini, so both exist at boot. The frontend's asset manifest leaves out the
  # derived inis, so an install's copies never shadow them.
  foreach(_ini Default.ini DefUser.ini)
    if(EXISTS "${CMAKE_SOURCE_DIR}/../Config/System/web/${_ini}")
      set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS
        " --preload-file ${CMAKE_SOURCE_DIR}/../Config/System/web/${_ini}@/System/${_ini}")
    endif()
  endforeach()
  # The preloaded inis are link inputs: LINK_DEPENDS relinks when they change.
  set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/../Config/System/web/Default.ini"
    "${CMAKE_SOURCE_DIR}/../Config/System/web/DefUser.ini")
  # The --js-library shims are link inputs too, which CMake does not infer from a link option.
  set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/../vendor/glue/net-shim.js"
    "${CMAKE_SOURCE_DIR}/../vendor/glue/wasmfs-shim.js"
    "${CMAKE_SOURCE_DIR}/../vendor/glue/pkgguid-shim.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/msaa-backbuffer.js")
  # ProxyNetDrv's own shim (the optional proxynet ABI module), a separate --js-library so that a build without
  # UE_WITH_PROXYNET has no ue_proxynet_* imports at all.
  if(UE_WITH_PROXYNET)
    target_link_options(${PROJECT_NAME} PRIVATE
      "SHELL:--js-library ${CMAKE_SOURCE_DIR}/../vendor/glue/proxynet-shim.js")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
      "${CMAKE_SOURCE_DIR}/../vendor/glue/proxynet-shim.js")
  endif()
  # Bare loader: SDLLaunch.js (+ .wasm/.data), no .html, into build-<cfg>/SDLLaunch/. `SDLLaunch` is the loader
  # basename the frontend expects from every engine.
  set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME SDLLaunch
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/SDLLaunch)

  # 24-bit depth for the emulated default framebuffer, which Emscripten allocates as GL_DEPTH_COMPONENT16
  # whatever the context asks for. offscreen-depth24.sh rewrites the emitted loader to D24S8 and fails the build
  # if the pattern count changes; POST_BUILD, so it runs on every relink.
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/offscreen-depth24.sh" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/SDLLaunch.js"
    COMMENT "offscreen-depth24: D24S8 for the emulated default framebuffer")

  # The artifact descriptor meta.json: abiVersion from the vendored ABI header, provenance and toolchain best
  # effort. The frontend's build validates it before assembling the served tree.
  file(STRINGS ${CMAKE_SOURCE_DIR}/../vendor/glue/ue_abi.h _ue_abi_line REGEX "define[ \t]+UE_ABI_VERSION")
  string(REGEX MATCH "([0-9]+)" UE_ABI_VERSION "${_ue_abi_line}")
  execute_process(COMMAND git -C ${CMAKE_SOURCE_DIR}/.. rev-parse --short HEAD
    OUTPUT_VARIABLE META_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  string(TIMESTAMP META_BUILT_AT "%Y-%m-%dT%H:%M:%SZ" UTC)
  # The toolchain file knows where Emscripten is (a preset build has only $EMSDK set, no emcc on PATH).
  set(META_EMCC "unknown")
  if(EXISTS "${EMSCRIPTEN_ROOT_PATH}/emscripten-version.txt")
    file(READ "${EMSCRIPTEN_ROOT_PATH}/emscripten-version.txt" _emcc_ver)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" META_EMCC "${_emcc_ver}")
  endif()
  # Every fetched library as repo@branch|tag, from the cache variables the fetch read (the web build always
  # has NOpenALDrv, hence openal-soft and libxmp).
  include(${CMAKE_SOURCE_DIR}/cmake/ArtifactDeps.cmake)
  set(META_DEPS)
  ufront_artifact_dep(META_DEPS openal-soft OpenALSoft "${OPENALSOFT_GIT_REPOSITORY}" "${OPENALSOFT_GIT_TAG}")
  ufront_artifact_dep(META_DEPS libxmp libxmp "${LIBXMP_GIT_REPOSITORY}" "${LIBXMP_GIT_TAG}" "${LIBXMP_SOURCE_DIR}")
  if(BUILD_XOPENGLDRV)
    ufront_artifact_dep(META_DEPS xopengl XOpenGLDrv "${XOPENGL_GIT_REPOSITORY}" "${XOPENGL_GIT_TAG}")
  endif()
  if(BUILD_UTGLR)
    ufront_artifact_dep(META_DEPS utglr utglr "${UTGLR_GIT_REPOSITORY}" "${UTGLR_GIT_TAG}")
    ufront_artifact_dep(META_DEPS gl4es GL4ES "${GL4ES_GIT_REPOSITORY}" "${GL4ES_GIT_TAG}" "${GL4ES_SOURCE_DIR}")
  endif()
  find_program(NODE_EXECUTABLE node REQUIRED)
  # The emitter is an input to the artifact: LINK_DEPENDS forces the relink that re-runs POST_BUILD.
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
