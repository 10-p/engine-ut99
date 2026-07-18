# XOpenGLDrv (v400 port) — engine-tree changes & driver-local adaptations

This port follows a **driver-only** rule: all XOpenGLDrv adaptations for vanilla **UT99 v400** (UE1)
live inside `Source/XOpenGLDrv/` and are gated `#if ENGINE_VERSION==400` (v400's `Core/Inc/UnObjVer.h`
already defines `ENGINE_VERSION 400`; the upstream fork's version matrix keys off `ENGINE_VERSION`, so a
future **Unreal v200** port adds parallel `#if ENGINE_VERSION==200` branches next to the 400 ones).

## 1. The ONLY engine-tree edit (required build glue — already applied)

`Source/CMakeLists.txt` — register the package (mirrors the adjacent `NOpenGLESDrv` block):

```cmake
if(NOT PLATFORM_DREAMCAST AND NOT EMSCRIPTEN)
  add_subdirectory(XOpenGLDrv)
  list(APPEND INSTALL_TARGETS XOpenGLDrv)
endif()
```

This is unavoidable to add any package to the build. It is pure glue (no engine logic). Nothing else
under `Source/Core|Engine|Render|IpDrv|Fire|NSDLDrv|...` is modified — verified via `git status`
(`M Source/CMakeLists.txt`, `?? Source/XOpenGLDrv/`).

## 2. M3 engine-tree edits — APPLIED, all gated on `WASM_USE_XOPENGL` (non-breaking)

The WebGL2/XOpenGL web build is opt-in via `-DWASM_USE_XOPENGL=ON` (top-level CMake option, default OFF).
When OFF, every one of these is a no-op and the existing NOpenGLESDrv web build is byte-for-byte unchanged.
The option sets an `add_compile_definitions(WASM_USE_XOPENGL)` so the driver-tree files below can gate on it.

- **`Source/CMakeLists.txt`** — the `WASM_USE_XOPENGL` option + `add_compile_definitions` (in the EMSCRIPTEN
  branch) + renderer selection (build XOpenGLDrv as the sole WASM renderer instead of NOpenGLESDrv).
- **`Source/UnrealTournament/CMakeLists.txt`** — when `WASM_USE_XOPENGL`, the emscripten link flags become
  `-sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sFULL_ES3=1 -sGL_ENABLE_GET_PROC_ADDRESS=1` (WebGL2/GLES3 +
  glad function-pointer loading); otherwise the original `-sFULL_ES2=1` (WebGL1) is used.
- **`Source/NSDLDrv/Src/NSDLViewport.cpp`** (~line 350) — under `__EMSCRIPTEN__ && WASM_USE_XOPENGL`, request
  an **ES 3.0** WebGL2 context (major=3, minor=0) instead of ES 2.0. (This is the change formerly deferred
  in §2a of this doc — now applied and build-gated so NOpenGLESDrv keeps its ES2 context.)
- **`Source/UnrealTournament/Src/Launch.cpp`** (~line 167) — under `WASM_USE_XOPENGL`, the emscripten boot
  forces `GameRenderDevice=XOpenGLDrv.XOpenGLRenderDevice` (else `NOpenGLESDrv.NOpenGLESRenderDevice`). The
  web boot hardcodes the render device in code (the shipped Default.ini is Dreamcast-configured), so this is
  where the web renderer is chosen.

Rationale for touching these engine-tree drivers/launcher: the web renderer is selected by the launcher +
windowing driver at boot, so a driver-only package cannot self-select. All edits are `WASM_USE_XOPENGL`-gated
and reviewable as one opt-in unit. (The monorepo's `targets/ut99/target.json` videoDriver was also switched to
XOpenGLDrv for the web build assembly — that lives in `ufront`, not this repo.)

## 3. How XOpenGLDrv integrates without touching the engine

- **Context reuse (native + web).** On v400, XOpenGLDrv does **not** create its own GL context. NSDLViewport
  already creates an SDL GL context and makes it current before calling `Init` (same as NOpenGLESDrv), so
  `CreateOpenGLContext` adopts it via `SDL_GL_GetCurrentContext()`; `Unlock` does not swap (NSDLViewport
  swaps); `Exit` does not delete it. All gated `#if ENGINE_VERSION==400` in `Src/XOpenGL.cpp`. This avoids
  any NSDLDrv change on native and keeps the diff inside the driver.
- **Package registration** = identical to NOpenGLESDrv: 3-arg `DECLARE_CLASS`, `IMPLEMENT_PACKAGE(XOpenGLDrv)`
  + `IMPLEMENT_CLASS`, `UPACKAGE_NAME=XOpenGLDrv` from CMake. No OldUnreal 4-arg DECLARE_CLASS / no
  `autoInitializeRegistrants*` (those don't exist in v400).

## 4. Driver-local v400 adaptations (all in `Source/XOpenGLDrv/`, gated `ENGINE_VERSION==400`)

- `Inc/XOpenGLDrv.h`: include `RenderPrivate.h` for v400 (skip the double-include of guard-less
  `UnRender.h`); compat shims `BOOL`/`TRUE`/`FALSE`/`PF_None`/`NAME_DevGraphics` (absent in v400 Core).
- `Inc/XOpenGL.h`: `clockFast`/`unclockFast` for v400; `PF_AlphaBlend`=0 (UT469 flag; its 469 value 0x20000
  collides with v400 `PF_CloudWavy`); `PF_Straight/Premultiplied_AlphaBlend`=0; `ERenderZTest` enum;
  `INT MaxTextureSize` member (normally from the 469 base class); `FIsCompressedFormat`/`FTextureFormatString`
  stubs; and **TEXF_ aliases** mapping the fork's 469 format names to v400's numeric `ETextureFormat`
  (`TEXF_BGRA8_LM→TEXF_RGBA7`, `TEXF_R5G6B5→TEXF_RGB16`, `TEXF_BC1→TEXF_DXT1`, `TEXF_BGRA8→TEXF_RGBA8`;
  `TEXF_RGBA8_`/`TEXF_RGB16_`/`TEXF_RGB10A2_LM` = dead sentinels).
- `Src/XOpenGL.cpp`: context reuse (see §3); `Viewport->SizeX/Y` instead of the 469-only
  `PhysicalSizeX/Y`; HW-light gather routed through the 430-family field layout (v400 `FPlane.X/Y/Z`,
  `Region.ZoneNumber` — not the 227/469 fields).
- `Src/DrawComplex.cpp` / `Src/DrawGouraud.cpp`: `Viewport->CurrentTime` for the bump-map `Lock` (no
  `FTime()`); `NoNearZ=FALSE` (no `GUglyHackFlags`/`HACKFLAGS_NoNearZ` in v400).
- `Src/SetTexture.cpp`: `appMsgf`→`GWarn->Logf` (no `appMsgf` in v400).
- `Src/ShaderProgram.cpp`: v400-safe `DumpShader` (v400 `TArray<FString>::AddItem` assigns onto
  unconstructed memory → realloc crash); narrow `ADD_OPTION` / ErrorLog format.

### GL profile selection: Core / ES / ES_WEBGL (driver-only, max-fidelity split)
The `OpenGLVersion` ini enum (`CPF_Config`, `[XOpenGLDrv.XOpenGLRenderDevice]`) gains a third value:
`Core` (0), `ES` (1), **`ES_WEBGL` (2)**. `ES` is the upstream desktop-GLES 3.1 path (io-blocks,
`#version 310 es`, `GL_OES_shader_io_blocks`) — kept byte-identical to the fork. `ES_WEBGL` is the WebGL2 ==
GLES 3.0 subset (flattened plain varyings, `#version 300 es`, S3TC via the WebGL extension name). Design:
- **One platform decision.** The only `#ifdef __EMSCRIPTEN__` that drives render behaviour is the single
  profile-selection line in `Init` (`OpenGLVersion = GL_ES_WEBGL`, because a browser can only give a WebGL2
  context). Everything else keys off the runtime profile via two helpers on the device:
  `IsES()` (true for `GL_ES` || `GL_ES_WEBGL`) and `IsWebGL()` (`GL_ES_WEBGL` only). Desktop honours the ini,
  so **`OpenGLVersion=ES_WEBGL` validates the WebGL path natively**.
- `SetOptionsForRendererConfig` sets `OPT_GLES` for both ES profiles and additionally `OPT_GLES_WEBGL` when
  `IsWebGL()`. The shader generators branch: `#if OPT_GLES_WEBGL` → flat varyings + `#version 300 es`;
  `#else` (ES 3.1 / Core) → io-blocks. `#version` selection in `EmitGlobals` is driven purely by
  `OPT_GLES_WEBGL` (no `ENGINE_VERSION` gate). Flattened paths live in `DrawTile_GLSL.cpp` (ES program,
  dual-path) and `DrawGouraud_GLSL.cpp` (5 `OPT_GLES_WEBGL` gates); the pre-existing `OPT_GLES` output-layout
  gate (no dual-source blend on ES) stays as-is since it applies to both ES profiles.
- `== GL_ES` comparisons were swept to `IsES()` (~5 sites); `== GL_Core` sites are unchanged (`ES_WEBGL`
  isn't Core, so they stay correct).
- A future v200 port reuses this verbatim: web selects `ES_WEBGL`, and `#if ENGINE_VERSION==200` branches
  sit beside the `==400` correctness fixes below.

### WebGL2/ES render-correctness fixes (v400 bring-up, all gated in-driver)
- `Src/ShaderProgram.cpp`: the `DistanceFogParams` uniform block was declared in **every** shader's GLSL
  unconditionally, but `glUniformBlockBinding`/the UBO for it are only set on `ENGINE_VERSION==227`. On
  v400 that left an active-but-**unbound** uniform block, which WebGL2/ANGLE flags on every `glDrawArrays`
  (`"used but unbound uniform buffer"`). Fixed by wrapping the block declaration in `#if OPT_DistanceFog`
  (its only consumer + the version where it is bound), so it isn't declared on v400.
- `Src/XOpenGL.cpp` (`SetGamma` / `GetViewportGamma`): `GammaMultiplier`/`GammaMultiplierUED` are
  `CPF_Config` floats whose StaticConstructor default (1.75) is **not applied on v400** — they load as 0 —
  and the web client's `Brightness` is also 0, so `Gamma = (Brightness+0.1)*GammaMult` computes to 0. The
  PostProcess present shader's `GammaCorrect()` then does `pow(color, 1.0/Gamma) = pow(color, 1/0) = 0`,
  i.e. a **fully black present** even though the scene renders correctly into the RenderFBO. Fix (both
  `#if ENGINE_VERSION==400`): `SetGamma` restores `GammaMultiplier<=0 → 1.75`; `GetViewportGamma` floors a
  returned Gamma `<1 → 1.0` (identity present; a real brightness slider can still raise it). This was
  latent on native too — we had only ever readback-verified the RenderFBO, never the post-gamma backbuffer.

### DXT1/S3TC compressed textures on WebGL2 (RESOLVED)
Compressed textures first rendered as a placeholder rainbow-checkerboard, then (once enabled) fatally
`appErrorf`'d with `"Unpacking unknown format TEXF_3"`. Two driver-only fixes:
- `Src/CheckExtensions.cpp`: the S3TC check only accepted the desktop name `GL_EXT_texture_compression_s3tc`.
  ANGLE/WebGL exposes S3TC as `WEBGL_compressed_texture_s3tc` (verified via a browser `getSupportedExtensions()`
  dump — the DXT1 token 0x83F1 is present and identical). Added the WebGL name(s) to the check under
  `#if ENGINE_VERSION==400`, so `SupportsS3TC` stays true and DXT1 uploads via `glCompressedTexImage2D`.
- `Src/SetTexture.cpp`: in the upload switch, `case TEXF_BC1` (== v400 `TEXF_DXT1`) had its
  `CompImageSize`/`break` trapped inside the `#if 227||UNREAL_TOURNAMENT_OLDUNREAL` block, so on v400 it fell
  through to the `default: appErrorf(...)`. Moved DXT1 out of that gate; and since the OLDUNREAL helper
  `FTextureBytes` doesn't exist on v400, compute the DXT1 size directly there
  (`Max(1,(U+3)/4)*Max(1,(V+3)/4)*8` — 8 bytes per 4x4 block).
Result: DXT1 textures render correctly on WebGL2 (metal-panel walls instead of checkerboard).

### Non-4-aligned DXT1 (software decode via vendored GL4ES decoder)
A few DXT1 textures have base dimensions that are not multiples of 4 (e.g. `GenFX.Gradient.Fibre2`, a
128x2 gradient). WebGL2/ANGLE rejects those via `glCompressedTexImage2D` ("invalid compressed image size").
For that case only, `Src/SetTexture.cpp` decodes the DXT1 mip to RGBA8 in software and uploads it
uncompressed (`DecodeDXT1` path; block-aligned DXT1 still uploads compressed to hardware). The block
decoder is **vendored straight from GL4ES** — `Src/decompress.cpp` + `Inc/decompress.h` (public-domain
Benjamin Dobell / Anteru S3TC decompressor; the DXT *compression*/stb_dxt tail was dropped, v400 only
decodes). `XOpenGLDecodeDXT1Image` wraps `DecompressBlockDXT1` with a block-padded buffer + compaction
(the block decoder writes full 4x4 blocks with no edge clip). Only active on the ES_WEBGL profile.

### Present gamma (v400 web)
`Src/XOpenGL.cpp` `SetGamma`: the web client's `Brightness` defaults to 0; substitute the retail default
(0.5) so the PostProcess `GammaCorrect` present isn't crushed to near-black. Combined with the
`GammaMultiplier<=0 -> 1.75` restore, Gamma computes to ~1.05 (retail-default look). `GetViewportGamma`
keeps a `Gamma<=0 -> 1.0` last-resort guard.

### Blanket `%ls` → `%s` (whole vendored copy, NOT per-version gated)
The upstream fork is **UNICODE** (`TCHAR = wchar_t`, uses `%ls`). Vanilla UE1 — **both v400 and v200** — is
**ANSI** (`TCHAR = char`), where `%s` is the correct TCHAR specifier and `%ls` silently drops the arg. This
corrupts *generated GLSL* (`#define` names emitted via `FString::Printf("...%ls...", name)`), uniform/block
names, extension-check strings, and empties all logs. Because every target in this repo's lineage is ANSI,
`%ls`→`%s` was applied as a **blanket transformation** to `Source/XOpenGLDrv/{Src,Inc}` rather than gated
per-version. (It is only "wrong" for the OldUnreal upstream, which this private copy never merges back to.)

## 5. Known non-issue: native on-screen present is black

On this dev box the native SDL-x11 (Xwayland) window shows **black for both XOpenGLDrv and the reference
NOpenGLESDrv** — a shared present/compositor quirk, not a renderer bug. Per the repo's design, native
on-screen was never the authoritative render gate (the web E2E is). XOpenGLDrv's native render is verified
by reading back the RenderFBO (`glReadPixels`), which shows the UT intro rendered correctly.
