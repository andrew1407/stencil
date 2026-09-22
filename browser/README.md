# Stencil — Browser app

The browser front-end of Stencil. Built with **vanilla JavaScript and native ES
modules** — no build step, no bundler, no third-party runtime dependencies. (There is one
*optional* build, [`npm run build`](#single-file-build), which packs the app into a single
self-contained HTML file; the app itself never depends on it.)

For the project overview see the [repository README](../README.md); for how this app is put
together, [`ARCHITECTURE.md`](ARCHITECTURE.md). Screenshots and clips, scenario by
scenario: [`usecases/docs/browser/USECASES.md`](../usecases/docs/browser/USECASES.md).

## Running

Browsers refuse to load ES modules over `file://`, so the app must be served over HTTP (or
packed into one file first, see [Single-file build](#single-file-build)):

```bash
# from this directory (browser/) — serves http://localhost:8080 by default
npm run serve

PORT=9000 npm run serve                # custom port
ADDR=0.0.0.0 PORT=3000 npm run serve   # bind all interfaces (LAN access)
```

Any other static file server works too.

**WebAssembly core.** The app runs the shared C++ core via wasm, but that module
(`js/wasm/stencilCore.js`) is a generated artifact that isn't committed — on a fresh checkout
the app transparently uses its behavior-identical JS fallback. To run the real wasm path,
build it once (needs Emscripten on `PATH` — see [`../core/WASM.md`](../core/WASM.md)):

```bash
npm run build-wasm   # builds core/ → js/wasm/stencilCore.js
```

### Single-file build

`npm run build` folds the whole app — every module, every stylesheet, the icons — into
**one self-contained `stencil.html`** that opens straight off disk. Handy for handing the
editor to someone as a single attachment, or for an air-gapped machine.

```bash
npm install                    # the sole dev dependency: vite
npm run build                  # -> browser/stencil.html (gitignored)
npm run build -- notes.html    # any name; a bare name gets .html appended
npm run build -- ~/dist/app    # any path, relative to where you ran it
```

The single file leaves out the wasm module (the JS fallback runs), the cross-tab worker
(cross-tab sync uses `BroadcastChannel`), the operator's `openInConfig.json` and the PWA
files. Projects still persist, the console API is fully wired, and servers / LLM endpoints
you configure are reachable as long as they send CORS headers for a `null` origin.

### GitHub Pages

[`.github/workflows/pages.yml`](../.github/workflows/pages.yml) publishes this app on every
push to `main`. It builds the wasm core first, so the deployed site runs the real C++ core,
and puts the single-file build alongside at `stencil.html`. Turn Pages on once under
**Settings → Pages → Source: "GitHub Actions"**; the manifest's paths are relative, so the
app and its service worker work under the `/<repo>/` subpath.

### Docker

The multi-stage [`Dockerfile`](Dockerfile) builds the wasm core and serves the static app
with nginx. Build **from the repo root** (the wasm step needs `core/`):

```bash
docker build -f browser/Dockerfile -t stencil-browser .
docker run --rm -p 8080:80 stencil-browser   # -> http://localhost:8080
```

## Using the editor

- **Drawing** — click to add points to a polyline; **New Line** starts the next one.
  Rectangles/areas can be locked and filled. **Hold-to-draw**: press and hold the left
  button to enter drawing and drop the first point, dwell to add more, release to commit;
  hold over an existing point to extend its line, over a line body to insert a point (delay
  in the Visuals modal). **Alt+Delete** removes the selected line, **Alt+Shift+Delete** the
  focused point.
- **Per-line style** — colour, thickness, point size, solid / dashed / dotted.
- **Points table** — edit coordinates numerically, with pixel ↔ page (cm) conversion and
  optional `f(x,y)` formula transforms. Either formula may use both `x` and `y`, a bare
  number, and the constants `PAGE_WIDTH`, `PAGE_HEIGHT` (in the selected display unit),
  `PAGE_WIDTH_CM` / `_IN`, `PAGE_HEIGHT_CM` / `_IN` and `IMAGE_WIDTH` / `IMAGE_HEIGHT` (in
  pixels). Page formats cover the full ISO A/B/C series plus a custom size.
- **Image** — filters (B&W, sepia, invert, contour, custom tint), crop, quarter-turn rotate,
  zoom/pan, fit-to-window, a blank-page creator, undo/redo, drag-and-drop and clipboard
  paste for images and layout JSON.
- **Windows** — Projects, Servers, Links, Assistant (`Alt+G`), Assistant settings
  (`Alt+Shift+G`), Shortcuts (`Alt+K`), Visuals (`Alt+V`), Help (`Alt+H`). A window's own
  shortcut closes it again. `Shift+F10` opens the canvas context menu from the keyboard.
  Every shortcut is rebindable in the Shortcuts window.
- **Appearance** — light/dark theme with preset accents in the Visuals modal (double-click
  the logo for a one-off custom accent). **Visuals → Motion** turns the drawing animation
  on/off and picks the interface animation: Dust (default), Water, Fire, Sliding or None;
  the OS's `prefers-reduced-motion` always wins.
- **Projects** — the session autosaves; projects are kept in the browser with a one-week
  expiry. In the Projects list a single click asks before opening here, a double click opens
  straight away, ⌘/Ctrl+click opens in a new tab; touch rows open on tap and reorder on
  press-and-hold.
- **PWA** — installable via the "Install app" button or the browser's install UI; runs in its
  own window and works offline.
- **Servers** — connect to a [collaboration server](../server/README.md) from the Servers
  window to share projects and co-edit live. A connected row offers **Invite**, which copies
  an `<server-url>#token=<token>` link; pasting one into the Connect URL field adopts its
  token.
- **Open in…** — the toolbar button next to Share hands the current session to the desktop
  app (a `stencil://` link) or the Telegram bot (server projects only). The bot option
  appears once you configure it: copy `js/config/openInConfig.example.json` to
  `js/config/openInConfig.json` (gitignored) and set `telegramBotUsername`.
  `launch.html` is a standalone bounce page that forwards a `#stencil-desktop=<url>` fragment
  to the `stencil://` scheme, for channels that don't linkify custom schemes.

### Scripts (`.stc`)

The script button in the toolbar's **Data** section (`Alt+Shift+S`, needs an image open) opens
the script window: write a `.stc` — crop, filter, draw, layout, save and undo written as `@`
directives — coloured as you type, and press **Run** (`Ctrl+Enter`) to apply it to the open
project. **Copy** puts the text on the clipboard, **Download** writes it out as `stencil.stc`,
**Upload** loads one back in. The window opens empty every time; nothing is kept between opens.

Mistakes are only reported once you press Run: the line is named under the editor and
underlined in place, and a script with any error runs nothing. A run that succeeds closes the
window; a failed one stays put with the text and the underlines still in front of you.

The canvas right-click menu's **Stencil Script** row opens the same editor as a compact flyout
beside the menu, so you can type and run without leaving the canvas — running or failing
leaves the menu open. On phones and touch pointers the row opens the window instead.

Dropping a `.stc` onto the page loads it into the editor while the script window is open, and
runs it on the current project when the window is closed.

The browser has no filesystem, so here `@source` and `@layout` name a URL rather than a local
path, and `@save` saves the open project (a name after it renames it). With no `@source` at
all the directives apply to whatever is already open:

```stc
# No @source: the directives apply to the project that is already open.
@use line #cccccc dashed, fill aqua
@rect (10, 10) (-10%, -10%)
@crop 5%
@filter sepia
@save
```

The language is the same one the [CLI's](../cli/README.md) `--script` takes, written up in
[`contracts/stc/stc-contract.md`](../contracts/stc/stc-contract.md); the shared example corpus
is [`js/config/script/fixtures/`](js/config/script/fixtures/cases.txt).

### Project files (`.stencil`)

A `.stencil` file is a single JSON document bundling a whole project — the original image,
the layout (lines, filter, crop, page, formulas), project metadata and, optionally, the
current colour theme — openable on any Stencil surface.

Save with the **Project** button in the Data section (`Ctrl+Shift+S`; **Shift+click** for a
theme-neutral file), open with the folder button next to it (`Ctrl+Shift+F`) or by dropping a
`.stencil` onto the canvas. The **live-sync** toggle (`Ctrl+Shift+Y`) keeps a file-linked
project auto-saved to its file on disk; the **trash** button (`Ctrl+Shift+Backspace`) deletes
the linked file after a confirm (Chromium only). `Ctrl+Alt+R` removes the current project
from the editor, `Ctrl+Alt+N` renames it, `Ctrl+Alt+S` shares the image where the browser
can reach the OS share sheet.

```jsonc
{
  "format": "stencil-project",
  "version": 1,
  "name": "road sign",
  "color": "#7c3aed",
  "image": { "dataUrl": "data:image/png;base64,iVBOR…", "ext": "png", "w": 1280, "h": 720 },
  "layout": { "imageWidth": 1280, "imageHeight": 720, "lines": [ /* … */ ],
              "cropRect": { "x": 0, "y": 0, "w": 1280, "h": 720 },
              "rotationQuarters": 0, "imageFilter": "none" },
  "theme": { "mode": "dark", "accent": "violet" }
}
```

### AI assistant

The sparkle toolbar button (`Alt+G`) opens a chat panel — dockable on any edge or floating;
on phones it is a centred modal. The same chat is the **Assistant ▸** flyout of the canvas
right-click menu. Describe an edit and the model answers with a validated op-plan executed
through the same operations as the toolbar (crop, rotate, filters, layouts including
vision-based line extraction, formulas, page formats, blanks), optionally as several variant
images. Paste or drop images and videos onto the panel to attach them (video frames are
extracted in-browser; the video itself is never sent).

Provider, endpoint, model and key live in the assistant settings modal (`Alt+Shift+G`), next
to the opt-in **"Save chats with projects"** toggle. Calling a local provider directly needs
its CORS allowance (Ollama `OLLAMA_ORIGINS`, LM Studio "enable CORS"); the full setup guide
is in the [root README](../README.md#ai-assistant--setting-up-a-model), the contract in
[`contracts/llm/`](../contracts/llm/llm-contract.md).

## Console API (`window.stencil`)

The editor exposes a chainable scripting API on `window.stencil`. It is a thin facade —
every mutation routes through the same core methods the toolbar uses, so scripting from the
console and clicking the UI stay in sync. Most calls return the facade (or a
`Project`/`Line`/`Point`) for chaining.

**The full surface is written down once, as types: [`js/console/stencilApi.d.ts`](js/console/stencilApi.d.ts).**
Orientation:

- **Settings** are get/set properties — `stencil.lineColor = 'red'`, `stencil.pageSize = 'a3'`
  — and every key works both on the facade and under `stencil.settings`; `apply({...})`
  sets many at once and chains: `stencil.apply({ page: 'a4', pointSize: 6 }).rotateLeft().crop({ x2: '-2cm' })`.
- **Session**: `await stencil.load(url, { frame, address, incognito })`, `await stencil.blank(color, { size })`,
  `newEditor()`, `save()`; **image**: `rotateLeft/Right()`, `crop({ x1, y1, x2, y2 } | { scale })`,
  `zoom()`, `undo()/redo()`, `clearLines()`; **export**: `downloadImage()`, `copyImage()`,
  `layout` (get/set), `applyLayout()`, `setLines()`, `saveProjectFile()` / `openProjectFile()`.
- **Projects**: `stencil.current`, `openedProjects`, `getProjectByName()` → a `Project`
  with `name`, `color`, `keywords`, `expire()`, `open()`, `close()`, `moveToServer()`.
- **Lines & points**: `stencil.lines[i]` → a `Line` (`apply()`, `move()`, `rotate()`, `add()`,
  `join()`); `line.points[j]` → a `Point` (`x`/`y` settable, `move()`, `remove()`).
- **Servers**: `connect()`, `disconnect()`, `serverProjects()`, `publishIncognito()`;
  **windows**: `openWindow('Projects')` or the named `open*Window()` openers, `closeWindow()`.
- **Scripts**: `await stencil.execScript(text)` runs a `.stc` against the open project;
  `stencil.checkScript(text)` parses only and returns the diagnostics, one string each.
- **Assistant**: `stencil.llm` (the settings, get/set), `await stencil.prompt(text, { images })`,
  and `stencil.chat` for the panel (`open()`, `dock()`, `history`, `abort()`, `clear()`).
- **Motion**: `stencil.drawingAnimations`, `stencil.motionMode`, `stencil.holdDrawDelay`.

The object is a frozen facade: reassigning a method or read-only field throws, and members
are non-enumerable so `console.log(stencil)` reads as `{}` while autocomplete still works.
`stencil.extension` is installed by the Chrome extension, not by this app — see
[`browser-extension/README.md`](../browser-extension/README.md) ("Editor mode") for its surface.

## Tests

```bash
# from this directory (browser/)
npm test          # or: node --test
```

Node's built-in runner, no dependencies. `node --test` never loads wasm — it always runs the
JS fallback; the wasm parity suite runs against a freshly built module in CI.
