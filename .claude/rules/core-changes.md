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

Note the ABI asymmetry: the CMake/wasm build compiles `wasmApi.cpp`; the CLI and pystencil
each append **`cliApi.cpp`** (the `extern "C"` ABI they wrap) instead. Keep only the pure
`core/*.cpp` modules synced across the three — the api file differs per surface by design.

## 2. Behavior parity — update the twin and the tests

Each core module is a port of a specific `browser/js/` call site (the mapping is at the top
of each core header) and the browser keeps a **JS fallback that must match the wasm build
op-for-op** (`browser/tests/wasm-parity.test.js` enforces it). So a behavioral change to a
core module also means:

- change the matching `browser/js/…` fallback so the two stay identical (the deliberate
  exception is `formulaEngine.js` `new Function` vs `core/parse/formulaParser` — keep their
  *contract* aligned, not their implementation; no `eval` in core),
- update **both** test suites (`core/tests/*` are ports of `browser/tests/*`),
- run `cd browser && npm run build-wasm && npm test` (the wasm-parity test) to confirm they
  didn't diverge.

## 3. Keep core pure

`core/` stays **STL-only, codec-free, GUI-free**. Don't pull in Qt, an image codec, HTTP,
JSON, or DOM access — those live in the adapters (Zig CLI, Qt/browser GUIs). See
`no-dependencies.md`.
