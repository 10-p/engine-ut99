# OpenGLDrv (UTGLR) on the web

On the web UTGLR draws through gl4es ([10-p/gl4es](https://github.com/10-p/gl4es), a fork of ptitSeb's: desktop GL
to GLES2), linked statically into the wasm (`USE_GL4ES`, on in the `if(EMSCRIPTEN)` branch). gl4es is fetched from
`GL4ES_GIT_REPOSITORY` at `GL4ES_GIT_TAG` (`10-p/gl4es`, `ufront`, the branch every engine fetches), or taken from
`GL4ES_SOURCE_DIR`, and built with `NOX11 NOEGL STATICLIB DEFAULT_ES=2 NO_INIT_CONSTRUCTOR`. The frontend lists the
device as `-UTGLRRENDERER` in its `targets/ut99/target.json`.

## Context

- NSDLViewport gives UTGLR a WebGL 2 context on the worker (`-WEBGL=1`, or a browser without WebGL 2, gives
  WebGL 1); gl4es's GLES2 calls run on either.
- SDL owns no context on the worker, so the fork points gl4es at `emscripten_webgl_get_proc_address` instead of SDL's
  loader, sets `LIBGL_ES=2`, `LIBGL_NOBANNER=1` and `LIBGL_USEVBO=0` (UTGLR feeds client-side arrays), then calls
  `initialize_gl4es()`. UTGLR resolves its entry points through `gl4es_GetProcAddress`.
- UTGLR draws into framebuffer 0, which under `-sOFFSCREEN_FRAMEBUFFER` is Emscripten's single-sample emulated one. For
  antialiasing (`UseAA`, `NumAASamples`) the fork asks `ufront_backbuffer_samples` (`Source/UnrealTournament/
  msaa-backbuffer.js`) for a multisampled back buffer at Init and logs `AA: <n>x multisampled back buffer` or `AA:
  off`. WebGL 1 has no multisampled renderbuffers.

## Fork behaviour under `__EMSCRIPTEN__`

- `SupportsFogMaps` is set in `Init`, so zone fog maps are generated.
- Screen-space overlay tiles draw with the depth test off.
- Without `ATI_texture_env_combine3`/`NV_texture_env_combine4` (gl4es on WebGL has neither) fog is its own
  single-texture `GL_MODULATE` pass.

## Settings

- `UseFragmentProgram` and `UsePalette` stay off: GLES2 through gl4es has no ARB programs or paletted textures.
- DXT1 uploads compressed when the browser exposes `WEBGL_compressed_texture_s3tc`; without it `SupportsTC` is 0 and
  the engine decompresses DXT1 to RGBA.
