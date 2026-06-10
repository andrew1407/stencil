# Stencil — Browser app

The browser front-end of Stencil. Built with **vanilla JavaScript and native ES
modules** — no build step, no bundler, no third-party runtime dependencies.

For the project overview and the desktop (C++/Qt) app, see the
[repository README](../README.md).

## Features

- Draw polylines and lockable, fillable rectangles/areas over an uploaded image
- Blank-image creator (white / black / any color, sized to the page by default) for
  starting from an empty canvas
- Per-line color, thickness, marker size, and style (solid / dashed / dotted)
- Editable points table with pixel ↔ page (cm) coordinate conversion and optional
  `f(x,y)` formula transforms
- Image filters (B&W, sepia, custom tint), zoom/pan, and fit-to-window
- Undo / redo, drag-and-drop and clipboard paste for images and layout JSON
- Configurable keyboard shortcuts, context menu, fullscreen, and light/dark theme
- Session autosave to `localStorage` (image + layout), multi-project storage with a
  one-week expiry sweep

## Running

Because the app uses native ES modules (`import` / `export`), browsers refuse to load it
over the `file://` protocol (CORS / module-origin restrictions). It must be served over
HTTP. The `serve` script uses Python's built-in server:

```bash
# from this directory (browser/) — serves http://localhost:8080 by default
npm run serve
```

Then open <http://localhost:8080/> in your browser.

The address and port default to `localhost:8080` and can be overridden with env vars:

```bash
PORT=9000 npm run serve                # custom port
ADDR=0.0.0.0 PORT=3000 npm run serve   # bind all interfaces (LAN access)
```

> If `python` maps to Python 2 on your system, use `python3`. Any other static file
> server works too.

> **WebAssembly core.** This app can run the shared C++ core via wasm, but that module
> (`js/wasm/stencilCore.js`) is a generated artifact that isn't committed — so on a fresh
> checkout the app transparently uses its behavior-identical JS fallback. To run the real
> wasm path, build it once (needs Emscripten on `PATH` — see [`../desktop/WASM.md`](../desktop/WASM.md)):
>
> ```bash
> npm run build-wasm   # builds desktop/core → js/wasm/stencilCore.js
> ```

## Project structure

```
index.html            # single <script type="module"> entrypoint
css/                  # theme, layout, component styles
js/
  index.js            # bootstraps the app on window load
  utils.js            # shared DOM / geometry / color / hotkey helpers
  config/             # constants, hotkey + help-text registries
  core/               # DrawingApp and its collaborators (renderer, storage,
                      #   history, zoom/pan, coord table, formulas, projects store)
  ui/                 # pure string-returning components composed by layout()
  worker/             # cross-tab projects sync worker + message constants
tests/                # node:test unit tests (run with `node --test`)
```

Every module declares its dependencies with `import` and exposes its public API with
`export`. The HTML loads only `js/index.js`; the module graph pulls in everything else.

## Tests

Unit tests (pure logic: formulas, history, geometry, color, hotkeys, projects store, and
static markup) run under Node's built-in test runner — no dependencies to install:

```bash
# from this directory (browser/)
node --test
# or
npm test
```

## Relationship to the C++ core

This app **runs the shared C++ core at runtime via WebAssembly**. At boot,
`js/index.js` calls `core.init()` on the `core` singleton in `js/core/stencilCore.js`,
which instantiates the compiled core (`js/wasm/stencilCore.js`, a generated artifact
built from `desktop/core/` — gitignored, see `desktop/WASM.md`) and installs typed
wrappers into that singleton. Each pure-logic module calls through it and keeps its
JS body as a fallback — used when the module hasn't been built or fails to load, and
by `node --test`, which never loads wasm. The C++ counterparts in `desktop/core/`:

| This app | C++ core |
|---|---|
| `js/core/formulaEngine.js` | `desktop/core/formulaParser.*` |
| `js/utils.js` (`distToSegment`, color) | `desktop/core/geometry.*`, `desktop/core/color.*` |
| `js/core/drawingApp.js` (`pixelToPageCoords`) | `desktop/core/pageMetrics.*` |
| `js/core/historyStack.js` | `desktop/core/historyStack.*` |
| `js/core/projectsStore.js` | `desktop/core/projectsStore.*` |

> Note: the C++ `formulaParser` is a real recursive-descent parser for `+ - * / ** ( )`,
> replacing this app's `new Function(...)` (`eval`) approach. When wasm is loaded,
> `formulaEngine.js` delegates to it; the JS `new Function` path remains as the
> fallback. Keep the two behaviorally aligned (same operators, same precedence,
> same identity-on-error semantics) so the fallback matches the C++.
>
> `historyStack.js` / `projectsStore.js` run as JS (their C++ counterparts exist
> but need a handle-based ABI rather than the flat numeric surface used by the
> rest); they are the natural candidates to route through wasm next.
