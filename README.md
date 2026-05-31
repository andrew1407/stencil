# Stencil

A browser-based image annotation / drawing tool. Load an image, draw polylines
and rectangles over it, edit points numerically, convert pixel coordinates to
page (cm) coordinates, and save your work to the browser's local storage.

Built with **vanilla JavaScript and native ES modules** — no build step, no
bundler, no third-party runtime dependencies.

## Features

- Draw polylines and lockable, fillable rectangles/areas over an uploaded image
- Per-line color, thickness, marker size, and style (solid / dashed / dotted)
- Editable points table with pixel ↔ page (cm) coordinate conversion and optional `f(x,y)` formula transforms
- Image filters (B&W, sepia, custom tint), zoom/pan, and fit-to-window
- Undo / redo, drag-and-drop and clipboard paste for images and layout JSON
- Configurable keyboard shortcuts, context menu, fullscreen, and light/dark theme
- Session autosave to `localStorage` (image + layout)

## Running

Because the app uses native ES modules (`import` / `export`), browsers refuse to
load it over the `file://` protocol (CORS / module-origin restrictions). It must
be served over HTTP. The `serve` script uses Python's built-in server:

```bash
# from the project root — serves http://localhost:8080 by default
npm run serve
```

Then open <http://localhost:8080/> in your browser.

The address and port default to `localhost:8080` and can be overridden with env
vars:

```bash
PORT=9000 npm run serve                # custom port
ADDR=0.0.0.0 PORT=3000 npm run serve   # bind all interfaces (LAN access)
```

> If `python` maps to Python 2 on your system, use `python3`. Any other static
> file server works too.

## Project structure

```
index.html            # single <script type="module"> entrypoint
css/                  # theme, layout, component styles
js/
  index.js            # bootstraps the app on window load
  utils.js            # shared DOM / geometry / color / hotkey helpers
  config/             # constants, hotkey + help-text registries
  core/               # DrawingApp and its collaborators (renderer, storage,
                      #   history, zoom/pan, coord table, tooltip, formulas)
  ui/                 # pure string-returning components composed by layout()
  features/           # init functions that wire up DOM behavior after mount
tests/                # node:test unit tests (run with `node --test`)
```

Every module declares its dependencies with `import` and exposes its public API
with `export`. The HTML loads only `js/index.js`; the module graph pulls in
everything else.

## Tests

Unit tests (pure logic: formulas, history, geometry, color, hotkeys, and static
markup) run under Node's built-in test runner — no dependencies to install:

```bash
node --test
# or
npm test
```
