# Stencil core → WebAssembly

The `desktop/core/` library is GUI-free and STL-only, so the same sources that
back the Qt desktop app can compile to WebAssembly and back the **browser** app —
replacing its hand-written JS engines with one shared, tested implementation.

`desktop/core/wasmApi.cpp` is a thin `extern "C"` surface over the core. The
`if(EMSCRIPTEN)` block in `desktop/CMakeLists.txt` builds it into
`stencil_core.js` + `stencil_core.wasm`.

> **Status:** the browser app today uses **no WebAssembly at all** — its logic is
> the hand-written JS in `browser/js/`. The core was only ever *designed* to be
> wasm-portable (STL-only, GUI-free); this is the **first** wasm path, and it is
> build-ready, not yet wired into the browser. `emcc` is **not installed** in this
> development environment, so the wasm artifact was **not built here**. The target
> is fully guarded — a normal native `cmake` build never enters the Emscripten
> branch and produces exactly the same `stencil_core`, `stencil_tests`, and
> `stencil_gui` targets as before.
>
> `browser/README.md` names this intended swap explicitly: "If/when the core is
> compiled to WebAssembly, `formulaEngine.js` is the intended call site to swap
> over to it." That has not happened yet — this is the first wasm path.

## What it replaces

| Browser JS today | Exported wasm function(s) |
|---|---|
| `core/formulaEngine.js` validate / apply / evaluate | `stencil_formulaValidate`, `stencil_formulaApply`, `stencil_formulaEvaluate` |
| `utils.js` `distToSegment` | `stencil_distToSegment` |
| `drawingApp.js` `getPageDimensions` / `pixelToPageCoords` (raw) | `stencil_pageDimensions`, `stencil_pixelToPageRaw` |
| `drawingApp.js` `#closeCurrentShape` gate | `stencil_shouldCloseShape` |
| `renderer.js` `drawImageWithFilter` / `#applyTintFilter` | `stencil_applyFilterRGBA` |
| `drawingApp.js` `#rotateSelectedLine` rotation + bbox pivot | `stencil_rotatePoints`, `stencil_boundingBoxCenter` |
| `zoomPan.js` clamp / zoom-toward / zoom-to-rect math | `stencil_clampScale`, `stencil_anchoredZoom`, `stencil_rectZoom` |

The image-filter math now lives once in `core/imageFilter.{hpp,cpp}`
(`filterPixel` / `applyFilterRGBA`); the desktop canvas routes its bw / sepia /
duotone-tint pixels through it, and `stencil_applyFilterRGBA` is the same code
for the browser. `applyFilterRGBA` takes a canvas `ImageData.data` buffer
(interleaved RGBA8) and filters it in place, preserving alpha — so the future JS
wiring computes grayscale + tint in one pass instead of a CSS `grayscale()`
followed by a per-pixel tint.

(`historyStack.js` and `projectsStore.js` remain available in the core; add
wrappers to `wasmApi.cpp` the same way if the browser should consume them too.
The multi-line hit-testers — `findLineAt` / `findNearestPoint` /
`findNearestSegment` — are still core-only: they take a whole `Lines` tree, which
wants a handle-based ABI rather than the flat `double*` surface used here.)

## Testing the ABI without Emscripten

`wasmApi.cpp` is plain STL, so it is compiled **natively into `stencil_tests`**
(see `CMakeLists.txt`) and every export is exercised by `tests/wasmApi.test.cpp`.
The marshalling (flat point arrays, output pointers, the filter-mode enum codes,
char-code variable names) is therefore covered on every build, even on a machine
without `emcc`. `core/imageFilter` has its own suite in
`tests/imageFilter.test.cpp`.

## Install Emscripten (emsdk)

```sh
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh   # puts emcc / emcmake / emmake on PATH
```

## Build the wasm module

From `desktop/`:

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
emmake cmake --build build-wasm -j
# -> build-wasm/stencil_core.js  +  build-wasm/stencil_core.wasm
```

`emcmake` defines `EMSCRIPTEN`, so only the `stencil_wasm` target is produced
(no Qt, no Doctest binary). The native build is untouched:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

## Wire it into the browser app

The module is built `MODULARIZE=1 EXPORT_ES6=1`, so it imports cleanly:

```js
import createStencilCore from './stencil_core.js';
const core = await createStencilCore();

// distToSegment(px,py,ax,ay,bx,by) -> number
const distToSegment = core.cwrap('stencil_distToSegment', 'number',
  ['number','number','number','number','number','number']);

// formula validate/apply (varName is the char code of 'x' or 'y')
const formulaValidate = core.cwrap('stencil_formulaValidate', 'number',
  ['string','number']);
const formulaApply = core.cwrap('stencil_formulaApply', 'number',
  ['string','number','number','number']);

console.log(formulaApply('x+9', 'x'.charCodeAt(0), 3, 1)); // 12
```

Functions that return multiple doubles (`stencil_pageDimensions`,
`stencil_pixelToPageRaw`, `stencil_anchoredZoom`, `stencil_rectZoom`) take an
output pointer: allocate a small Float64 buffer with `_malloc`, pass it, then
read it back with `core.getValue(ptr + i*8, 'double')` and `_free` it.

```js
const out = core._malloc(3 * 8);                       // {scale, scrollX, scrollY}
core.ccall('stencil_rectZoom', null,
  ['number','number','number','number','number','number','number'],
  [50, 50, 100, 100, 400, 400, out]);
const scale = core.getValue(out, 'double');
core._free(out);
```

Then swap the bodies of `formulaEngine.js`, the geometry helpers in `utils.js`,
and the page-calc helpers in `drawingApp.js` to delegate to these calls. Because
the wasm is compiled from the same source the desktop uses, the two front-ends
stay in lock-step by construction.

## Notes / decisions

- **`extern "C"` over embind.** The surface is plain C functions over doubles and
  C strings, exposed via `ccall`/`cwrap` with no extra runtime — minimal and
  ABI-stable. embind would also work but is Emscripten-only and heavier; if
  adopted it must live solely in this translation unit and never link into the
  desktop build.
- The wasm translation unit stays **STL-only + core** (no Qt), exactly like the
  rest of `core/`.
