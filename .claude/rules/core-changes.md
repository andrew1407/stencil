---
description: Hidden couplings when changing core/ — the 3-file source sync and wasm/JS parity
paths:
  - "core/**"
  - "cli/build.zig"
  - "pystencil/build.py"
---

# Changing `core/`

`core/` is the shared C++ logic three surfaces recompile independently. Two couplings here
are invisible from the file you're editing — a smart edit that ignores them fails in a
*different* subproject's CI. Before finishing a `core/` change:

## 1. Source list — edit all THREE in lockstep

Adding, removing, or renaming a `core/*.cpp` means updating the identical file list in **all
three** build definitions, or the cli and pystencil builds break with a link error nowhere
near your edit:

- `core/CMakeLists.txt` → `STENCIL_CORE_SOURCES`
- `cli/build.zig` → the core sources array
- `pystencil/build.py` → the core sources list

Note the ABI asymmetry: the CMake/wasm build compiles the `wasm*Api.cpp` files; the CLI and
pystencil each append **`cliApi.cpp`** (the `extern "C"` ABI they wrap) instead. Keep only the
pure `core/*.cpp` modules synced across the three — the api file differs per surface by design.
**Consequence: splitting a wasm ABI file is a ONE-file edit.** `wasmApi.cpp`, `wasmCropApi.cpp`,
`wasmStateApi.cpp` and `wasmProjectsApi.cpp` appear only in `core/CMakeLists.txt`, twice each —
the `stencil_tests` exe and the `if(EMSCRIPTEN)` exe. Neither `cli/build.zig` nor
`pystencil/build.py` mentions them.

A **new wasm export** is a fourth, separate list: add `_stencil_x` to `EXPORTED_FUNCTIONS`
in `core/CMakeLists.txt` or the browser cannot call it.

## 2. Behavior parity — update the twin and the tests

Each core module is a port of a specific `browser/js/` call site (the mapping is at the top
of each core header) and the browser keeps a **JS fallback that must match the wasm build
op-for-op** (`browser/tests/wasm-parity.test.js` enforces it). So a behavioral change to a
core module also means:

- change the matching `browser/js/…` fallback so the two stay identical — including
  `browser/js/core/formulaEngine.js` ↔ `core/parse/formulaParser`, which are **both real
  recursive-descent parsers** (no `eval`, no `new Function`, on either side) and must agree
  operator for operator, down to `MAX_DEPTH` ↔ `kMaxDepth`,
- update **both** test suites (`core/tests/*` are ports of `browser/tests/*`),
- run `cd browser && npm run build-wasm && npm test` (the wasm-parity test) to confirm they
  didn't diverge.

## 3. Keep core pure

`core/` stays **STL-only, codec-free, GUI-free**. Don't pull in Qt, an image codec, HTTP,
JSON, or DOM access — those live in the adapters (Zig CLI, GUIs). See
`no-dependencies.md`.
