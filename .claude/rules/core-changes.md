---
description: Hidden couplings when changing core/ — the source, include-dir and wasm-export lists, wasm/JS parity, and no exceptions
paths:
  - "core/**"
  - "cli/build.zig"
  - "pystencil/build.py"
---

# Changing `core/`

`core/` is the shared C++ logic three surfaces recompile independently. These couplings are
invisible from the file you're editing — a smart edit that ignores them fails in a
*different* subproject's CI, or only in the wasm build. Before finishing a `core/` change:

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
**Consequence: splitting a wasm ABI file is a ONE-file edit.** Every `core/wasm*Api.cpp` appears
only in `core/CMakeLists.txt`, twice — the `stencil_tests` exe and the `if(EMSCRIPTEN)` exe. Neither `cli/build.zig` nor
`pystencil/build.py` mentions them.

A **new wasm export** is a separate list: add `_stencil_x` to `EXPORTED_FUNCTIONS` in
`core/CMakeLists.txt` and register it under `browser/js/core/abi/`, or the browser cannot call
it. The native build and `node --test` stay green without it; only a fresh wasm build fails.

## 2. Include-dir lists — a new folder is three more edits

Core headers include each other bare (`#include "cropGeometry.hpp"`), resolved through one
include path per group folder. A **new folder** under `core/` (or a moved group) joins all
three lists, or the other two builds fail to find its headers:

- `core/CMakeLists.txt` → `STENCIL_CORE_INCLUDE_DIRS`
- `cli/build.zig` → `core_include_dirs`
- `pystencil/build.py` → `INCLUDE_DIRS`

## 3. Behavior parity — update the twin and the tests

Each core module is a port of a specific `browser/js/` call site (the mapping is at the top
of each core header) and the browser keeps a **JS fallback that must match the wasm build
op-for-op** (`browser/tests/wasm/wasm-parity.test.js` enforces it). So a behavioral change to a
core module also means:

- change the matching `browser/js/…` fallback so the two stay identical — including
  `browser/js/core/parse/formulaEngine.js` ↔ `core/parse/formulaParser`, which are **both real
  recursive-descent parsers** (no `eval`, no `new Function`, on either side) and must agree
  operator for operator, down to the shared `MAX_DEPTH`,
- update **both** test suites (`core/tests/*` are ports of `browser/tests/*`),
- run `cd browser && npm run build-wasm && npm test` (the wasm-parity test) to confirm they
  didn't diverge.

## 4. Keep core pure

`core/` stays **STL-only, codec-free, GUI-free**. Don't pull in Qt, an image codec, HTTP,
a JSON library, or DOM access — those live in the adapters (Zig CLI, GUIs); the one JSON
reader core has is its own, STL-only, in `core/json/`. See `no-dependencies.md`.

Mind what a call links into wasm: `std::to_chars(double)` adds ~150 KB of Ryu tables and
iostreams pull in locales. Format a number through `json::jsNumberToString` (JS `String(n)`);
key a number by its bits. Compare `browser/js/wasm/stencilCore.js` before and after a core change.

## 5. Never throw

The wasm build has **no exception support**: a `throw` — or a library call that throws — aborts
the whole module in the browser, while every native build and `node --test` stay green. So core
never writes `throw`, `try` or `catch`, and never calls the throwing helpers:

- numbers: `std::from_chars`, or `core/parse/decimal.hpp` where the browser's `parseFloat`
  semantics matter — never `std::stoi` / `std::stod` and friends;
- lookups: `find` / `count` / `std::get_if` and an explicit miss path — never `.at()` or
  `std::get` on the wrong alternative;
- no `std::regex` — a hand-written matcher.

Report failure through the return value (`std::optional`, a result struct, an error code across
the ABI). `core/tests/noThrowLint.test.cpp` scans the tree and fails on any of these.
