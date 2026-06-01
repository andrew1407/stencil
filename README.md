# Stencil

[![CI](https://github.com/andrew1407/stencil/actions/workflows/ci.yml/badge.svg)](https://github.com/andrew1407/stencil/actions/workflows/ci.yml)

An image annotation / drawing tool: load an image, draw polylines and rectangles over
it, edit points numerically, convert pixel coordinates to page (cm) coordinates with
optional `f(x,y)` formula transforms, and save your work.

Stencil ships as **two front-ends over one shared logic core**:

| App | Path | Stack | Docs |
|---|---|---|---|
| **Browser** | [`browser/`](browser/) | Vanilla ES-module JS, no build step | [browser/README.md](browser/README.md) |
| **Desktop** | [`desktop/`](desktop/) | C++17 + Qt 6, CMake build | [desktop/README.md](desktop/README.md) |

The two apps deliberately mirror each other's architecture. The **pure, GUI-free logic**
— the formula parser, geometry, pixel↔page conversion, history, project storage and
expiry — lives once per language and is kept behaviorally identical between them. The C++
core (`desktop/core/`) is written dependency-free (STL only) so it can also be compiled to
**WebAssembly** and back the browser app from the same source in the future.

## Repository layout

```
README.md             # this overview
browser/              # the browser app
  index.html
  css/  js/  tests/
  package.json
  README.md
desktop/                  # the desktop app
  core/               # shared, GUI-free logic + its Doctest tests
  gui/                # Qt widgets (mirrors the browser UI)
  tests/
  third_party/        # vendored doctest.h
  CMakeLists.txt
  README.md
```

## Development

**Dependency policy:** the only permitted third-party libraries are **Qt 6** (desktop
GUI) and **Doctest** (C++ tests); the browser app stays dependency-free with no build
step.

- Build & run the browser app → [browser/README.md](browser/README.md)
- Build, test & run the desktop app → [desktop/README.md](desktop/README.md)

## License

See repository.
