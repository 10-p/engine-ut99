# SoftDrv on the web — what the native build does not tell you

Read this before changing anything under `Source/SoftDrv/`. A change can be correct, and validated
natively, and still be broken on the web, because the two builds do not share the code paths that
matter most to this renderer. That is not hypothetical: it is how the 2.13.2 framebuffer overrun
survived from 2026-07-30 to 2026-08-05, and how it came to be blamed on an innocent branch.

## 1. Native and web take different rasteriser code

This is the v400 UT99 SoftDrv, whose inner loops exist twice: hand-written x86 `__asm`, and portable
C under `#if !ASM`. **The web build is `ASM=0`** (`Source/SoftDrv/CMakeLists.txt`) and on x86 native
`GIsMMX` is force-disabled when `!ASM`, but a native build can still select MMX/Pentium variants at
runtime via `ColorMode`.

So "I validated it natively" does not establish that the portable path is right, and vice versa.
Anything touching `DrawSurf` / `DrawPoly` / `DrawTile` inner loops needs both.

## 2. Native and web draw into different memory

| | native | web (worker) |
|---|---|---|
| target | SDL streaming texture | `UNSDLViewport::SoftFB`, a plain heap buffer |
| obtained in `Lock` via | `SDL_LockTexture` | direct pointer |
| presented in `Unlock` by | SDL | `putImageData` to a transferred `OffscreenCanvas` |
| resized by | SDL, on texture re-create | **us, explicitly** |

The web has its own buffer because SDL's Emscripten renderer is GLES-backed: `SDL_CreateRenderer`
goes through `eglCreateContext`, which Emscripten proxies to the browser main thread — where the
canvas has been transferred to the worker and `getContext()` throws. The proxied call never returns
and the engine thread blocks forever. See the 2.13.2 comment in `NSDLViewport.cpp`.

**The consequence:** on the web, viewport size and buffer size are two facts that can disagree.
Natively they cannot — SDL owns both. Every bug of this shape is therefore web-only *and invisible to
a native run*, no matter how careful.

## 3. The web viewport size is the browser window, and it changes

`ResizeViewport` adopts the real drawable size over the ini one, because SDL2's Emscripten backend
maps the window to the CSS-sized (`100vw`/`100vh`) canvas. Two things follow that repeatedly catch
people out:

- **the ini resolution is not the rendered resolution.** A test that passes at the ini size proves
  nothing about the size the user actually gets;
- **it changes after startup**, via `WINDOWEVENT_RESIZED` in `TickInput`. The window-creation block
  is inside `if( !hWnd )` and does **not** re-run.

`SoftFB` is therefore sized in `Lock` — the single point where the buffer is about to be written and
`SizeX`/`SizeY` are final. Please keep it there. Sizing it at either the allocation site or the
adopt-drawable-size block leaves one of the two paths stale, which is exactly the bug that shipped.

## 4. Memory is not free — the static tables cost the same at every resolution

The `SizeX`/`SizeY`-indexed scratch tables (`Setup`, `MMXSetup`, `Photon`, `UCoordTable`, two
`SpanIndex`) are **static arrays bounded by `MaximumX/YScreenSize`**. They cost their full size in
`.bss` whether the viewport is 4K or 240×240 — 368 KB at the 4096 default, measured. Adding another
screen-indexed static is not free, and it is not proportional to what is being drawn.

Use `-DSOFTDRV_MAX_SCREEN=n` to lower the ceiling on constrained targets (256 → 23 KB). Viewports
above the ceiling degrade fail-soft: `SetRes` clamps X, `DrawPoly` skips the frame above the Y
ceiling, `DrawTile` clamps its span-fill.

## 5. A clean `-O2` run does not clear a memory bug

The 2.13.2 overrun did **not** reproduce under `-O2 -g`; only `-O3` (`wasm-perf`, what ships) faulted,
and it faulted three different ways — `memory access out of bounds`, `table index is out of bounds`,
`null function` — depending on what the write happened to land on. A heap layout that absorbs an
overrun is not a heap layout that lacks one. If a rasteriser bug appears and disappears across
optimisation levels, assume corruption and go looking for the write, not the read.

## 6. How to actually test a SoftDrv change on the web

`packages/e2e/tools/softdrv-matrix.cjs` in the monorepo drives the launcher's renderer dropdown,
boots a map and waits **past level load** — a rasteriser can come up and die on the first real draw:

```bash
SOFT_URL=https://localhost:8092 SOFT_CASES="1920x1080:DM-Codex,640x480:DM-Codex" \
  node tools/softdrv-matrix.cjs
```

Vary the **resolution**, not just the map. A single default-size smoke test is what let this through:
1024×768 passed while every size the owner actually plays at crashed.
