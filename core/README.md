# Stencil — Core (C++17, STL-only)

The shared logic that backs every Stencil front-end. A small, **GUI-free, STL-only**
C++17 library: formula parsing, geometry & hit-testing, colour, pixel↔page (cm)
conversion, crop, a software line rasteriser, image filters, undo/redo history and
project storage. It never includes Qt, touches the DOM, or links a codec — pure
functions over plain values and caller-owned RGBA8 buffers.

The browser runs it as WebAssembly, the desktop links it, and the CLI and Python package
recompile it and drive it over the `extern "C"` ABI in [`cliApi.h`](cliApi.h). Structure,
design rules and the parity contract: [`ARCHITECTURE.md`](ARCHITECTURE.md); the wasm build:
[`WASM.md`](WASM.md).

## Build & test

```bash
cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release
cmake --build core/build -j
ctest --test-dir core/build --output-on-failure   # or run core/build/stencil_tests directly
```

(From inside this directory drop the `core` prefix: `cmake -S . -B build`.)

The only dependency is the C++17 standard library, plus one header for the tests — Doctest,
pinned at v2.4.11 and fetched into `third_party/doctest.h` at configure time with SHA-256
verification. Nothing to install or commit.

Run a single case with `core/build/stencil_tests -tc="<case>"`. The benchmarks are skipped
by default: `core/build/stencil_tests -ts=bench --no-skip`. The wasm target is produced
only when configured through `emcmake` (see [`WASM.md`](WASM.md)).
