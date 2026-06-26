# Stencil — Browser app

The browser front-end of Stencil. Built with **vanilla JavaScript and native ES
modules** — no build step, no bundler, no third-party runtime dependencies.

For the project overview and the desktop (C++/Qt) app, see the
[repository README](../README.md).

## Features

- Draw polylines and lockable, fillable rectangles/areas over an uploaded image
- **Hold-to-draw**: press and hold the left button (no modifiers) to auto-enter
  drawing and drop the first point, then dwell to add more and release to commit —
  hold over an existing point to extend its line, over a line body to insert a
  point. Delay is configurable (Visuals modal / `stencil.holdDrawDelay`). Delete a
  selected line with **Alt+Delete** (⌥⌫ on macOS) or a focused point with
  **Alt+Shift+Delete** (⌥⇧⌫)
- Blank-image creator (white / black / any color, sized to the page by default) for
  starting from an empty canvas
- Per-line color, thickness, marker size, and style (solid / dashed / dotted)
- Editable points table with pixel ↔ page (cm) coordinate conversion and optional
  `f(x,y)` formula transforms
- Image filters (B&W, sepia, custom tint), zoom/pan, and fit-to-window
- Undo / redo, drag-and-drop and clipboard paste for images and layout JSON
- Configurable keyboard shortcuts, context menu, fullscreen, and light/dark theme
  (preset brand accents in the Visuals modal; **double-click the logo** for a one-off
  custom accent colour, applied to that page only — not saved or synced)
- Session autosave to `localStorage` (image + layout), multi-project storage with a
  one-week expiry sweep
- Installable as a **PWA** (Progressive Web App): "Install app" button + browser
  install UI, runs in its own window, and works offline via a service-worker cache

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
> wasm path, build it once (needs Emscripten on `PATH` — see [`../core/WASM.md`](../core/WASM.md)):
>
> ```bash
> npm run build-wasm   # builds core/ → js/wasm/stencilCore.js
> ```

### Docker

A multi-stage [`Dockerfile`](Dockerfile) builds the wasm core (Emscripten) and serves
the static app with nginx. Because the wasm step needs `core/`, **build from the repo
root** and select the Dockerfile with `-f`:

```bash
# from the repo root
docker build -f browser/Dockerfile -t stencil-browser .
docker run --rm -p 8080:80 stencil-browser   # -> http://localhost:8080
```

The image bakes in the freshly built `js/wasm/stencilCore.js`, so it runs the real wasm
path (no JS fallback). Map a different host port with e.g. `-p 9000:80`.

## Project structure

```
index.html            # single <script type="module"> entrypoint
manifest.webmanifest  # PWA metadata (name, icons, standalone display)
sw.js                 # service worker: offline app-shell + runtime cache
favicon.svg           # icon (also the PWA "any"-purpose icon)
icon-maskable.svg     # full-bleed PWA icon for adaptive (maskable) masks
css/                  # theme, layout, component styles
js/
  index.js            # bootstraps the app on window load
  pwa.js              # registers the service worker (best-effort)
  utils.js            # shared DOM / geometry / color / hotkey helpers
  config/             # constants, hotkey + help-text registries
  core/               # DrawingApp and its collaborators (renderer, storage,
                      #   history, zoom/pan, coord table, formulas, projects store)
  ui/                 # pure string-returning components composed by layout()
                      #   (incl. installButton.js — the PWA install affordance)
  worker/             # cross-tab projects sync worker + message constants
tests/                # node:test unit tests (run with `node --test`)
```

Every module declares its dependencies with `import` and exposes its public API with
`export`. The HTML loads only `js/index.js`; the module graph pulls in everything else.

## Console API (`window.stencil`)

The editor exposes a chainable scripting API on `window.stencil` (built in
`js/console/stencilApi.js`, wired in `js/index.js`). It is a thin facade — every
mutation routes through the **same shared core methods the toolbar uses**
(`setColor`, `setPageSize`, `applyCrop`, `loadImageFromFile`, …), so scripting from the
console and clicking the UI stay in sync. Most calls return the facade (or a
`Project`/`Line`/`Point`) for chaining.

The whole object is a **hard-guarded, frozen facade**: settings, lines, points, and
projects all reject reassigning a method or read-only field (`stencil.load = 0` throws),
so only the documented setters below mutate anything. `console.log(stencil)` reads as a
clean `{}` (members are non-enumerable) — access and autocomplete still work.

```js
// ── Settings (each is get/set; every key works BOTH on the facade and under .settings,
//    and mirrors a top-menu control — changes reflect in the toolbar live) ──
stencil.lineColor        = 'red';      // current/last-used line color — any CSS color (named / rgb()/hsl() → normalized to hex)
stencil.thickness        = 3;          // line thickness (px)
stencil.pointSize        = 9;          // marker size (alias: markerSize)
stencil.markerSize       = 9;          // same as .pointSize
stencil.lineStyle        = 'dashed';   // 'solid' | 'dashed' | 'dotted'
stencil.pointStyle       = true;       // points visible? (alias: showPoints)
stencil.showPoints       = true;       // show point markers
stencil.showLines        = true;       // show connecting lines
stencil.filter           = 'sepia';    // image filter: 'none' | 'bw' | 'sepia' | 'custom'
stencil.filterColor      = '#7c3aed';  // tint color when filter === 'custom'
stencil.unit             = 'in';       // page unit: 'cm' | 'mm' | 'in'
stencil.pageSize         = 'a3';       // case-insensitive: 'a3' | 'A4' | 'custom'
stencil.pageWidth        = 30;         // cm; applies when pageSize === 'custom'
stencil.pageHeight       = 40;         // cm; applies when pageSize === 'custom'
stencil.darkTheme        = true;       // dark mode on/off (true = dark, false = light)
stencil.mainTheme        = 'green';    // brand accent: a preset key (see .mainThemes) — persists + syncs across tabs
stencil.mainTheme        = '#ff5623';  // …or any hex → a custom accent for THIS page only (not saved, not synced)
stencil.drawMode         = 'rect';     // 'line' | 'rect'
stencil.holdDrawDelay    = 500;        // hold-to-draw hold/dwell delay, ms (clamped 100–3000)
stencil.allowFormulas    = true;       // enable the f(x,y) coordinate transforms
stencil.formulaX         = 'x*2';      // x transform (also formulaY)
stencil.formulaY         = 'y+10';
stencil.fillColor        = '#3399ff';  // default rect/area fill
stencil.selectionGlow    = '#ffd400';  // visuals: selection glow color
stencil.hoverRing        = '#22c55e';  // visuals: hover ring color
stencil.focusRing        = '#7c3aed';  // visuals: focus ring color
stencil.settings.lineColor = '#f00';   // …or namespace them all under stencil.settings.<key>

// ── Modes & view ──
stencil.fullscreen = true;             // get/set fullscreen editor mode
stencil.incognito  = true;             // get/set (only on a blank editor; edits won't be saved)
stencil.imageSize;                     // { width, height } of the loaded image (or undefined)
stencil.tooltip.enabled = true;        // tooltip sections (get/set): enabled / page / screen / coords
stencil.tooltip.page = false;
stencil.zoomLevel = 150;               // absolute zoom % (get/set)
stencil.zoom(0.25);                    // relative zoom step (+ in / − out) → facade
stencil.zoom(1, { x: 100, y: 80 });    // …keeping image point (100,80) fixed on screen
stencil.zoomFit();                     // fit image to window → facade

// ── Image actions (each returns the facade for chaining) ──
stencil.rotateLeft();  stencil.rotateRight();
stencil.undo();        stencil.redo();
stencil.startDrawing();  stencil.stopDrawing();   // enter / leave point-adding mode
stencil.drawing = true;                // …or toggle it (get/set; needs a loaded image)
stencil.clearLines();                  // remove all lines
stencil.newEditor();                   // clear to a fresh blank (unsaved) editor
await stencil.blank('red', { size: { width: 800, height: 600 } });  // blank image to draw on
await stencil.blank();                 // white, sized to the current page (any CSS color)
stencil.crop({ x1: '10%', y1: '10%', x2: '-10%', y2: '-10%' });  // %, '3cm'/'-4in', px; '-' = from end
stencil.move({ x: 10, y: -5 });        // pan the view by px
stencil.downloadImage();               // download image + lines (PNG)
stencil.copyImage();                   // copy the rendered image to the clipboard
stencil.copyLayout();                  // copy the layout JSON to the clipboard
stencil.downloadLayout();              // download the layout JSON
stencil.layout;                        // get the current layout object
stencil.layout = layoutObject;         // apply (import) a layout object

// Bulk-apply + chain (apply() takes any settings key plus tooltip/zoom/crop/move/layout):
stencil
  .apply({ page: 'a4', pointSize: 6, lineColor: 'aqua', tooltip: { screen: true } })
  .rotateLeft()
  .crop({ x2: '-2cm' });

// ── Load an image — or a video frame — by URL (resolves to the facade) ──
(await stencil.load('https://example.com/pic.png', { source: 'https://example.com/pic.png' }))
  .crop({ x2: '-2cm' })
  .apply({ lineColor: '#123456' });
await stencil.load('https://example.com/clip.webm', { frame: 1.5 });   // grab the frame at 1.5s

// ── Coordinate conversion ──
stencil.px2Page({ x: 100, y: 100 });   // → { x, y } in page cm (formulas applied)
stencil.page2Px({ x: 5, y: 5 });       // → { x, y } in pixels

// ── Projects ──
stencil.current;                       // the active Project (or null on a blank editor)
stencil.openedProjects;                // open in some tab/window (incl. this tab's incognito)
stencil.archivedProjects;              // saved but not open anywhere
stencil.incognitoProjects;             // this tab's incognito project, if any
stencil.getProjects({ archived: true, incognito: true });   // filtered list
const p = stencil.getProjectByName('Floor plan');
p.id; p.incognito; p.isOpened; p.isExpired; p.expiresAt; p.layout;   // getters
p.size;                                // { image: { width, height } }
p.name = 'Floor plan v2';              // get/set; throws on a duplicate name
p.imageName = 'plan.png';              // get/set (active project only)
p.source = 'https://example.com/x.png';   // get/set provenance link (updates live)
p.resource = 'https://example.com/page';  // get/set the page the image came from
p.renew();                             // restart the 7-day expiry
p.open();                              // switch this tab to the project
p.close({ fully: false });             // drop the editor (fully:true also closes the tab)

// ── Lines & points (the current line's points are stencil.points) ──
stencil.lines;                         // array of Line wrappers
const line = stencil.lines[0];
line.idx; line.points;                 // getters
line.color = '#f00'; line.thickness = 4; line.markerSize = 8;   // get/set
line.style = 'dotted'; line.fillColor = '#3399ff';              // get/set
line.apply({ style: 'dashed', pointSize: 8 }).move({ x: 10 }).rotate(15, { x: 0, y: 0 });
line.add({ x: 120, y: 40 }, { neighbour: 0, after: true });     // insert a point
line.remove(2);                        // remove by index or point ref
line.join(stencil.lines[1]);           // append another line's points and drop it
const pt = line.points[0];
pt.lineIdx; pt.ptIdx; pt.x; pt.y;      // getters (x/y also settable)
pt.x = 50; pt.y = 60;                  // absolute set (px)
pt.apply({ x: 50, y: 60, size: 7 }).move({ x: 5, y: -3 });
pt.remove();                           // drop this point (empties the line → line is dropped)

// ── Shortcuts ──
stencil.shortcuts;                     // { undo: 'Ctrl+Z', … }
stencil.changeShortcut('Ctrl+Z', 'Ctrl+Alt+U');   // by current combo or action id
```

> An extension-side `window.stencil` (opt-in, for scanning/opening images on any
> page) is planned as a separate, default-off feature — see `extension/`.

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
built from `core/` — gitignored, see `core/WASM.md`) and installs typed wrappers into
that singleton. Each pure-logic module calls through it and keeps its JS body as a
fallback — used when the module hasn't been built or fails to load, and by
`node --test`, which never loads wasm. The C++ counterparts in `core/`:

| This app | C++ core |
|---|---|
| `js/core/formulaEngine.js` | `core/formulaParser.*` |
| `js/utils.js` (`distToSegment`, color) | `core/geometry.*`, `core/color.*` |
| `js/core/drawingApp.js` (`pixelToPageCoords`) | `core/pageMetrics.*` |
| `js/core/historyStack.js` | `core/historyStack.*` |
| `js/core/projectsStore.js` | `core/projectsStore.*` |

> Note: the C++ `formulaParser` is a real recursive-descent parser for `+ - * / ** ( )`,
> replacing this app's `new Function(...)` (`eval`) approach. When wasm is loaded,
> `formulaEngine.js` delegates to it; the JS `new Function` path remains as the
> fallback. Keep the two behaviorally aligned (same operators, same precedence,
> same identity-on-error semantics) so the fallback matches the C++.
>
> `historyStack.js` / `projectsStore.js` run as JS (their C++ counterparts exist
> but need a handle-based ABI rather than the flat numeric surface used by the
> rest); they are the natural candidates to route through wasm next.
