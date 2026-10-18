# SoftDrv on the web

`SoftDrv.SoftwareRenderDevice` is Epic's v400 software renderer (`-SOFTWARERENDERER`). Native and web run the same
rasteriser code but draw into different memory, so a change validated natively can still be broken on the web.

## One rasteriser path

Every build compiles SoftDrv with `ASM=0` (`Source/SoftDrv/CMakeLists.txt`), so the inner loops are the portable C
under `#if !ASM`, not Epic's x86 `__asm`. Without ASM, MMX is off on x86 (`SoftDrvPrivate.h`) and `GIsMMX` is set only
by the MSVC platform layer, so `Lock` picks the Pentium colour modes (`Pentium15/16/32`) everywhere.

## Two framebuffers

| | native | web (worker) |
|---|---|---|
| target | an SDL streaming texture | `UNSDLViewport::SoftFB`, a heap buffer |
| obtained in `Lock` by | `SDL_LockTexture` | a direct pointer |
| presented in `Unlock` by | SDL | `putImageData` to the transferred canvas through a 2D context |
| resized by | SDL, when the texture is re-created | `Lock`, when the size changed |

The web has its own buffer because SDL's Emscripten renderer would create a context through EGL, which Emscripten
proxies to the main thread, where the canvas has been transferred to the worker and the call never returns.

So on the web the viewport size and the buffer size are two values that can disagree; natively SDL owns both.

## The viewport is the browser window

The viewport adopts the canvas's drawable size over the ini resolution, and the size changes after startup through
the `SDL_WINDOWEVENT_SIZE_CHANGED`/`RESIZED` handler in `TickInput` (the window-creation block does not run again).
`SoftFB` is sized in `Lock`, the one point where the buffer is about to be written and `SizeX`/`SizeY` are final;
keep it there.

## Static tables

The screen-indexed scratch tables (`Setup`, `MMXSetup`, `Photon`, `FogPhoton`, `UCoordTable`, the two `SpanIndex`) are
static arrays bounded by `MaximumXScreenSize`/`MaximumYScreenSize` (`SOFTDRV_MAX_SCREEN`: 4096, 8192 on the web
presets). They cost their full size at every resolution. A viewport above the bound degrades: `SetRes` clamps the
size, `DrawPoly` skips the frame above the height bound, `DrawTile` clamps its span fill.

## Testing a change

A memory bug in the rasteriser may show only at one optimisation level: test `wasm-perf`, the shipping build, not only
`wasm`. Vary the resolution as well as the map. The frontend's `packages/e2e/tools/softdrv-matrix.cjs` picks the
software renderer in the launcher, boots each case and waits past level load:

```bash
SOFT_URL=https://localhost:8092 SOFT_CASES="1920x1080:DM-Codex,640x480:DM-Codex" node tools/softdrv-matrix.cjs
```
