# Stencil — Browser app

The browser front-end of Stencil. Built with **vanilla JavaScript and native ES
modules** — no build step, no bundler, no third-party runtime dependencies. (There is one
*optional* build, [`npm run build`](#single-file-build), which packs the app into a single
self-contained HTML file; the app itself never depends on it.)

For the project overview and the desktop (C++/Qt) app, see the
[repository README](../README.md).

## Architecture

```mermaid
graph TD
    CORE["<b>core/</b> — shared C++ logic"]
    WASM["js/wasm/stencilCore.js<br/><i>generated from core/, gitignored</i>"]
    subgraph APP["browser/ — vanilla ES modules, no build step"]
      IDX["js/index.js — bootstraps DrawingApp"]
      COREJS["js/core/ — DrawingApp + collaborators<br/><i>renderer · storage · history · zoom/pan · projects</i>"]
      UI["js/ui/ — string-returning components<br/><i>composed by layout()</i>"]
      API["js/console/ — window.stencil facade"]
    end
    FB["behavior-identical JS fallback<br/><i>used when wasm absent / under node --test</i>"]
    EXT["Chrome extension"]
    SRV["Collaboration server"]

    CORE -->|"Emscripten → WebAssembly · wasmApi.cpp"| WASM
    WASM --> COREJS
    IDX --> COREJS
    IDX --> UI
    IDX --> API
    COREJS -.->|"if wasm unavailable"| FB
    EXT -->|"feeds images · via URL fragment"| IDX
    COREJS -.->|"connect · REST + WS"| SRV
```

> **Surface diagrams:** [core](../core/README.md#architecture) · [extension](../extension/README.md#architecture) · [server](../server/README.md#architecture) — or the whole-system view in the [repository README](../README.md#architecture).

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
- Per-line color, thickness, point size, and style (solid / dashed / dotted)
- Editable points table with pixel ↔ page (cm) coordinate conversion and optional
  `f(x,y)` formula transforms; page formats cover the full ISO A/B/C series (A0–C10,
  searchable selector) plus a custom size
- Image filters (B&W, sepia, invert, contour edges, custom tint), zoom/pan, and
  fit-to-window
- Undo / redo, drag-and-drop and clipboard paste for images and layout JSON
- Configurable keyboard shortcuts, context menu, fullscreen, and light/dark theme
  (preset brand accents in the Visuals modal; **double-click the logo** for a one-off
  custom accent colour, applied to that page only — not saved or synced)
- **Motion settings** (Visuals modal → Motion): *Drawing animation* on/off — the stroke
  flight, landing pop and ripple on the canvas — and *Interface animation* with five
  settings: **Dust** (the default: windows, menus, checkbox marks, chat entries, the
  canvas and the listening mic all form out of round specks), **Water** (the same flights
  as drops) and **Fire** (as embers) — both painted in `--accent` / `--accent-2` rather
  than the surface's own pixels — **Sliding** (no particles anywhere: each surface plays
  its own plain grow/rise/fade instead) and **None**. Each mode's dropdown row wears its
  glyph and plays it on hover (`js/ui/motionIcons.js`). The OS's `prefers-reduced-motion`
  still overrides all of it. Also on the facade (`stencil.drawingAnimations`,
  `stencil.motionMode`) and mirrored, grain for grain, by the desktop app and the
  extension
- Session autosave (image + layout), multi-project storage with a one-week expiry
  sweep. The image-heavy per-project payloads live in **IndexedDB** (no ~5MB
  localStorage ceiling; existing payloads migrate over on first load), while the
  small project registry — names, thumbnails, expiry — stays in `localStorage`
- **Opening a project from the list, by gesture**: a **single click** asks first
  ("Open … here?") and switches this tab on confirm; a **double click** opens it
  straight away; **⌘/Ctrl + click** asks and opens in a **new tab**; **⌘/Ctrl + double
  click** opens a new tab immediately. The single click waits one double-click interval
  (250 ms) before acting, so a double click never flashes the confirmation open and shut.
  Rows are keyboard-activatable (**Enter/Space** = the confirmed single click, ⌘/Ctrl
  held targets a new tab). On **touch** a **tap** asks and opens here; **"Open in new
  tab" lives in the row's ⋯ menu** there, because press-and-hold is already how the list
  picks a row up for **drag-to-reorder** (`touchDrag.js`, 280 ms) — that gesture is left
  alone rather than overloaded. **Movement always wins**: as soon as a pointer travels
  past ~10 px the gesture belongs to the drag or the scroll, any pending open is dropped,
  and the click a drop may synthesize is swallowed — reordering never opens a project
- Installable as a **PWA** (Progressive Web App): "Install app" button + browser
  install UI, runs in its own window, and works offline via a service-worker cache
- **Open in… (desktop app / Telegram bot)**: the toolbar button next to Share mirrors
  the current session into another Stencil front-end. A server-linked project sends
  only the server reference (`{url, id, version}` — never a token; the receiver
  connects like a fresh client, minting a token via `POST /auth/token` or prompting);
  a local/incognito project embeds the image + full layout inline in a `stencil://`
  link the OS routes to the installed desktop app. The Telegram option (server
  projects only — a 64-char `?start=` payload can't carry bytes) appears once your
  bot is configured; payloads that can't fit the limit fall back to copyable
  `/connect` + `/fetch` commands. **Config** is the static-site equivalent of a
  `.env` file: copy `js/config/openInConfig.example.json` to
  `js/config/openInConfig.json` (gitignored, per-operator) and set
  `telegramBotUsername` (your bot's `@name` without the `@`). It loads at runtime, so
  a fresh clone with no local file still boots — the Telegram option just stays hidden.
  "Incognito" always means Stencil's own never-persisted mode on the receiving side.
  Inbound, the `#stencil=` fragment accepts `server` / `dataUrl` / `src` / `layout`
  fields (see `js/core/deepLink.js` `normalizeLaunchPayload`). `launch.html` is a
  standalone helper that forwards `#stencil-desktop=<encoded stencil:// URL>` to the
  OS scheme — useful for opening a `stencil://` link shared through a channel that
  won't linkify custom schemes (it validates the target is exactly `stencil:`). The
  Telegram bot's `/link` builds exactly that URL (`buildDesktopBounceUrl`, ported in
  its `DesktopLinkBuilder`), pointed at its `STENCIL_BOT_BROWSER_URL` app
- **Project files (`.stencil`)**: save a whole project — original image, layout, and
  settings (plus, optionally, the current colour theme) — as one portable file, openable
  on any Stencil surface (browser, CLI, desktop, pystencil, bot). See below.
- **Toolbar windows** (projects, servers, links, assistant, assistant settings `Alt+Shift+G`, shortcuts `Alt+K`, visuals `Alt+V`, help `Alt+H`): a window's own shortcut **closes** it again, another window's shortcut swaps to that one, and every one of them works from inside a window's own search box. Scripts open them by title — `stencil.openWindow('Projects')`, or the named `stencil.openProjectsWindow()` / `openServersWindow()` / `openAssistantSettingsWindow()` / … (Console API below).
- **Canvas context menu from the keyboard**: `Shift+F10` (rebindable, `contextMenu`) opens the right-click menu under the pointer when it rests over the canvas, else at the viewport centre — the desktop binds the same chord.
- **Server invite links**: in the Servers window, a connected row with a saved
  credential offers an **Invite** action — it mints a fresh session token
  (`POST /auth/token`, label `invite`) and copies `<server-url>#token=<token>` to the
  clipboard. Pasting an invite link into the Connect **URL** field (Token left empty)
  adopts the fragment token as the credential; an explicitly entered Token wins over
  the fragment, and the fragment itself never goes over the wire.
- **AI assistant chat**: the sparkle toolbar button (Alt+G) opens a chat panel dockable
  left/right/top/bottom or free-floating (drag the header to move or to re-dock on an
  edge zone, corner handle to resize; placement and size are session-only — every load
  starts closed at the defaults, and only the LLM settings persist). On **phones**
  (`max-width: 680px`) none of that fits, so the panel presents as an ordinary **centred
  modal** over a dimmed backdrop, with the placement and resize affordances hidden and
  the usual modal dismissals — a tap on the backdrop, or Escape. Attachments can also be pasted (Ctrl+V) or dropped straight onto the
  open panel; hover the input row's "…" for live provider status, and open the assistant's settings from that menu or with `Alt+Shift+G` (rebindable, `openAssistantSettings`). Prompts run against your configured LLM — Ollama or any local
  OpenAI-compatible server (LM Studio etc.) called directly, or Anthropic Claude proxied
  by a connected collaboration server (the API key stays server-side). The model answers
  with a validated *op-plan* executed through the same `window.stencil` facade as the
  toolbar (crop, quarter rotates, filters/tint, layouts — including vision-based "extract
  the lines from this image", formulas, page formats, blanks), and can return several
  variant images per prompt. It can also adjust the editor itself (contract §10): theme,
  accent, default line style, display units, points/lines visibility, and connect/
  disconnect of collaboration servers — restricted to servers you have already saved
  (exact URL or unique host; the stored connection's own token is used, plans never
  carry tokens), and never inside variants. **Every turn carries a snapshot of the working
  image** (contract §7), so "outline the rabbit's head" is answered against the pixels on
  the canvas rather than from memory; a model that has no vision gets it dropped, with a
  note in the chat, and the turn is retried once so text-only edits still work. On top of
  that you can attach images ("use as working image" or "analyze") and
  videos (frames are extracted in-browser; the video itself is never sent). Provider,
  endpoint URL (editable, sensible localhost defaults), model, and key are configured in
  the chat settings modal, alongside an opt-in **"Save chats with projects"** toggle
  (off by default; contract §12): when on, the conversation is stored **per project** in
  IndexedDB (text only, most recent 32 turns, never in incognito), restored when you
  reopen the project, mirrored onto a linked server project's `chat` file, removed with
  the project, and deleted — locally and server-side — by the panel's Clear button.
  Contract: `llm-contract.md`. Note: calling local
  providers directly requires their CORS allowance (Ollama `OLLAMA_ORIGINS`, LM Studio
  "enable CORS"). Runnable setup — installing/serving a model, the CORS commands,
  verification and troubleshooting — is in the
  [root README](../README.md#ai-assistant--setting-up-a-model).
  The canvas **right-click menu carries the same chat**: an **Assistant ▸** entry sits with
  the other submenu parents (Style, Image Filter, …) and its **flyout is the chat** — ask,
  send (Enter; Shift+Enter for a newline), watch the transcript, and press the same button
  again to **stop** a turn, without ever opening the panel. Its composer carries the panel's
  full action row — **send · attach · settings gear** (with the provider-status dot):
  attaching images/videos queues them on the same shared controller (chips show in both
  surfaces, and the queue rides whichever surface sends next), and the gear closes the menu
  and opens the one assistant-settings modal. It is the **same conversation** as the panel
  (one controller for the app, so history is continuous whichever surface you use) with the
  same op-plan capabilities and the same result cards (download · open as the working image). The menu and its flyout **stay open** through the whole exchange —
  typing, sending, and a plan executing on the canvas; the menu still closes on an outside
  click, on Escape (including from the chat input), or when you choose any other menu item,
  and hovering another submenu parent hands the flyout over as usual. On **phones and touch
  devices** (`max-width: 680px` or a coarse pointer — a hover-opened flyout is unusable
  there) the entry is a plain item instead: it closes the menu and opens the chat panel,
  which is already a full-screen modal at those sizes, on the same conversation. The entry
  appears only when a provider is configured: with the assistant off (provider *None*) the
  context menu is exactly what it has always been.

## Project files (`.stencil`)

A `.stencil` file is a **single JSON document** that bundles a whole project so it can be
moved between machines and surfaces: the **original** image (base64 `data:` URL, with
crop/rotation kept in the layout), the shared export **layout** (lines + filter + crop +
page + formulas), project **metadata** (name, accent, keywords, provenance, blank fill),
and an **optional** local colour **theme** (light/dark + accent) written only when you opt in.

Save with the **Project** button in the Data section (or `Ctrl+Shift+S`; **Shift+click** to
save a theme-neutral file), open with the folder button next to it (`Ctrl+Shift+F`) or by
dropping a `.stencil` onto the canvas. The **live-sync** toggle (`Ctrl+Shift+Y`) keeps a
file-linked project auto-saved to (and watching) its `.stencil` on disk. The **trash** button
(`Ctrl+Shift+Backspace`) deletes the linked `.stencil` from disk after a confirm — the project
stays open in the editor, only its on-disk file is removed (Chromium only; enabled once linked).
The same file opens in the CLI (`stencil -i project.stencil out.png`, or `/open` in `--console`),
the desktop app, `pystencil` (`Editor.open_project`), and the Telegram bot.

Three more project/image chords, all rebindable in the Shortcuts window: `Ctrl+Alt+R`
**removes the current project** from the editor (the red trash in Data — the session, not
the file), `Ctrl+Alt+N` **renames** it (opens the toolbar name field for inline editing,
like the ✎ beside it), and `Ctrl+Alt+S` **shares the image** wherever the browser can hand
files to the OS share sheet (mobile / PWA — the Share button is hidden elsewhere).

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

The (de)serializer is `js/core/projectFile.js` (pure, DOM-free, reuses `layout.js`'s
`buildLayoutPayload` + `sanitizeLines`); IO lives in `ExportService`
(`saveProjectFile` / `openProjectFile`, exposed on `window.stencil`). It's an adapter-level
format — **not** part of `core/` — so each surface serializes it independently; the `e2e/`
cross-surface check (author in the browser, open in the CLI) guards that they agree.

## Running

Because the app uses native ES modules (`import` / `export`), browsers refuse to load it
over the `file://` protocol (CORS / module-origin restrictions). It must be served over
HTTP — or packed into one file first, see [Single-file build](#single-file-build). The
`serve` script uses Python's built-in server:

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

### Single-file build

`npm run build` folds the whole app — every module, every stylesheet, the icons — into
**one self-contained `stencil.html`** that opens straight off disk, no server involved.
Handy for handing the editor to someone as a single attachment, or for an air-gapped
machine.

```bash
npm run build                  # -> browser/stencil.html (gitignored)
npm run build -- notes.html    # any name; a bare name gets .html appended
npm run build -- ~/dist/app    # any path, relative to where you ran it
```

This is the one place the browser app uses a build step, and it stays strictly optional —
the app itself is unchanged and `npm run serve` never touches it. It needs the sole dev
dependency, **vite** (`npm install` in this directory); the bundling rules are written out
inline in [`vite.config.js`](vite.config.js) so no plugin packages come with it. Its
`package-lock.json` IS tracked, so CI installs with `npm ci`, which pins vite's own
dependency ranges fresh on each run.

Every build ends by re-reading what it just wrote ([`tools/assertSelfContained.js`](tools/assertSelfContained.js)):
no `src`/`href` may point at a sibling file, no local JSON may have been baked in, and the
inline module has to parse — a page truncated by a stray `</script` in a string looks
perfectly fine until you open it. CI runs
the same build on every push (the *Browser (single-file build)* job), and
[`tests/singleFileBuild.test.js`](tests/singleFileBuild.test.js) fails the ordinary
`npm test` if an edit to `index.html` or one of the three loaders outruns the rewrites in
[`tools/singleFilePatterns.js`](tools/singleFilePatterns.js) — no vite install needed.

What a single file gives up, and why nothing breaks:

| Sibling file | In `stencil.html` |
|---|---|
| `js/wasm/stencilCore.js` | not bundled — the app runs its JS reference implementations (the ones the wasm build is parity-tested against) |
| `js/worker/projectsWorker.js` | not bundled — cross-tab coordination drops to its `BroadcastChannel` path, as on any browser without `SharedWorker` |
| `js/config/openInConfig.json` | not bundled — the builder's own operator config stays on their disk; the Open-in modal uses its defaults |
| `sw.js`, `manifest.webmanifest` | dropped — offline is moot for a file already on disk, and there is no shell to install |

Everything else is intact: projects still persist (IndexedDB + `localStorage` work from
`file://`), the `window.stencil` console API is fully wired, and servers/LLM endpoints you
configure are reachable as long as they send CORS headers for a `null` origin.

### GitHub Pages

[`.github/workflows/pages.yml`](../.github/workflows/pages.yml) publishes this app on every
push to `main` (and on demand via *Run workflow*). It builds the wasm core with Emscripten
first, so the deployed site runs the **real C++ core**, not the JS fallback — then serves
`browser/` as the site root, with the single-file build alongside at `stencil.html` as a
copy visitors can save and keep offline.

Nothing deploys until Pages is turned on for the repo: **Settings → Pages → Build and
deployment → Source: "GitHub Actions"**. The manifest's `start_url`/`scope` are relative, so
the app and its service worker work unchanged under the `/<repo>/` subpath Pages serves from.

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
vite.config.js        # the OPTIONAL single-file build (npm run build) — nothing else uses it
launch.html           # standalone bounce page: #stencil-desktop=<url> → stencil:// scheme
manifest.webmanifest  # PWA metadata (name, icons, standalone display)
sw.js                 # service worker: offline app-shell + runtime cache
favicon.svg           # icon (also the PWA "any"-purpose icon)
icon-maskable.svg     # full-bleed PWA icon for adaptive (maskable) masks
css/                  # theme.css, then layout/ (14), components/ (22 plus chat/ 8)
                      #   and animations/ (16) — one file per banner section of the
                      #   old layout.css / components.css / animations.css;
                      #   index.html's link order IS the cascade
js/
  index.js            # bootstraps the app on window load
  pwa.js              # registers the service worker (best-effort)
  utils.js            # one import point over utils/: DOM, geometry, color, hotkeys
  bus/                # appBus.js — the app-wide event channel (js/config/events.json)
  net/                # abortable fetch, connection store + manager, remote sync
  config/             # constants, hotkey + help-text registries, and the
                      #   cross-surface assets the other front-ends read:
                      #   themeTokens.json (css/theme.css's tokens, keyed),
                      #   mediaTypes.json (image/video extensions + format
                      #   normalisation), uiStrings.json (chat chips, the §12
                      #   disclosure, the windows registry), events.json (the
                      #   stencil:* channels — see events.README.md),
                      #   motion.json (every tuned duration/count/easing
                      #   js/ui/motion.js + dustCloud.js run on),
                      #   svgArt.json (the inline art that is not a 24-grid
                      #   glyph: the draw-mode pair + the accent favicon)
  core/               # DrawingApp and its collaborators (renderer, storage,
                      #   history, zoom/pan, coord table, formulas, projects store)
  llm/                # AI-assistant chat: provider client, op-plan parser/executor,
                      #   chat controller, the app's one shared chat session
                      #   (chatSession.js — panel + context menu), settings
                      #   (see llm-contract.md)
  console/            # the window.stencil facade — stencilApi.js composes one
                      #   module per concern (projects, connect, assistant, windows,
                      #   crop, session, editor/export actions, settings)
  ui/                 # pure string-returning components composed by layout()
                      #   (incl. installButton.js — the PWA install affordance);
                      #   bindings/ wires the toolbar controls to the app, and
                      #   motion/ holds the sections motion.js re-exports
  worker/             # cross-tab projects sync worker + message constants
tools/                # dev scripts: static server, single-file build + its self-check
tests/                # node:test unit tests (run with `node --test`)
```

Every module declares its dependencies with `import` and exposes its public API with
`export`. The HTML loads only `js/index.js`; the module graph pulls in everything else.

### Motion (`js/ui/motion/` + `css/animations/`)

These effects share one import point — `js/ui/motion.js` is a barrel over
`js/ui/motion/`, a file per section. All of them are decoration, so a browser without
`IntersectionObserver`/`MutationObserver` — or a user with `prefers-reduced-motion: reduce`
— just gets the static view, never a stuck one.

- **Scroll reveal** — `observeReveal(root, selector)` fades and lifts rows in as they enter
  a scroller and back out as they leave. Used by the assistant transcript (both the panel
  and the context-menu flyout, bound inside `renderChatLog`) and the projects list. The
  observer picks up new rows itself, so callers never re-scan after a render.
- **Drop landing** — a dropped image's canvas scales up into place with one accent pulse
  (`.canvas-container.drop-landing`), and the drop overlay leaves on an animation instead
  of blinking out (`.drop-closing`, click-through while it plays).
- **Disintegration** — a removed row doesn't fade, it comes apart: `disintegrate()` paints
  one grain per grid cell — a speck, a drop or a spark, by the motion setting, in the
  theme's accent and its shade — and scatters them, each on its own bent path, in a
  top-down sweep while the row's own box collapses so the list closes the gap. The
  particles are drawn on one canvas in a fixed layer over the page
  (`js/ui/dustCloud.js`), because the row under them is collapsing to zero height at the
  same moment. Clearing the image plays the same idea on
  the canvas (`ghostOut()` copies the pixels first — `clearRect` is instant and leaves
  nothing to animate — and flies the painted cells as grains).
- **Theme / accent swap** — `themeSwap()` floods the new palette out of the CONTROL that
  changed it (the moon button, the Visuals accent picker, the logo), as a growing circle,
  via the native View Transitions API; without it every colour consumer just gets one beat
  of transition (`html.theme-swapping`). With no such control on screen — a collapsed
  toolbar, a change pushed from another tab — the circle blooms from the viewport centre;
  it is never anchored to the last click, which is how it used to end up in a corner.
- **Fullscreen stretch** — `flipFrom(el, rectBefore)` plays the canvas viewport's new box
  out of the one it had, so entering fullscreen stretches out of the editor's canvas box
  and leaving minimises back into it. `.flip-active` lifts the viewport above the page
  chrome and suppresses its scrollbars for the flight.

The desktop app mirrors these (`desktop/src/support/scrollReveal.hpp`,
`ThemeSwapOverlay.hpp`, `dustKit.hpp`, and `MainWindow::consumeDropReveal` /
`beginFullscreenZoom`); the extension ports the reveal and the disintegration into
`extension/src/lib/motion/`, behind the same barrel shape (`lib/motion.js`).

## Console API (`window.stencil`)

The editor exposes a chainable scripting API on `window.stencil` (built in
`js/console/stencilApi.js`, wired in `js/index.js`). It is a thin facade — every
mutation routes through the **same shared core methods the toolbar uses**
(`setColor`, `setPageSize`, `applyCrop`, `loadImageFromFile`, …), so scripting from the
console and clicking the UI stay in sync. Most calls return the facade (or a
`Project`/`Line`/`Point`) for chaining.

**The full surface is written down once, as types: [`js/console/stencilApi.d.ts`](js/console/stencilApi.d.ts)**
(the `Stencil` interface, plus `StencilSettings`, `Project`, `Line`, `Point`, `LlmFacade`,
`ChatFacade` and the option bags). `tests/dts.test.js` keeps it in step with the module,
and `e2e/helpers/boot.js` drives the app through it. Orientation:

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
- **Assistant** (llm-contract.md): `stencil.llm` (the §5 settings, get/set), `await stencil.prompt(text, { images })`
  through the panel's own pipeline, and `stencil.chat` for the panel (`open()`, `dock()`, `history`, `abort()`, `clear()`).

The whole object is a **hard-guarded, frozen facade**: settings, lines, points, and
projects all reject reassigning a method or read-only field (`stencil.load = 0` throws),
so only the declared setters mutate anything. `console.log(stencil)` reads as a clean
`{}` (members are non-enumerable) — access and autocomplete still work.

`stencil.extension` is installed by the extension, not by this app: the facade only
re-exports what its content script put on the page, so the extension owns the method list —
see `extension/README.md` ("Editor mode") for the full surface and the Options toggle that
gates it. The editor side of that conversation is `js/core/extensionBridge.js`, which answers
the extension's state/import/switch requests through the same core methods everything else
uses (`loadImageFromFile` / `replaceProjectImage` / `switchToProject`).

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
| `js/core/formulaEngine.js` | `core/parse/formulaParser.*` |
| `js/utils/geometry.js` (`distToSegment`), `js/utils/color.js` | `core/geometry/pointMath.*`, `core/color/color.*` |
| `js/core/pageMetrics.js` (`pixelToPageCoords`) | `core/page/pageMetrics.*` |
| `js/core/historyStack.js` | `core/state/historyStack.*` |
| `js/core/projectsStore.js` | `core/state/projectsStore.*` |

> Note: both sides are real recursive-descent parsers for `+ - * / ** ( )` — there is
> no `eval`/`new Function` anywhere. When wasm is loaded, `formulaEngine.js` delegates
> to the C++ one and keeps its own parser as the fallback. Keep the two behaviorally
> aligned (same operators, same precedence, same identity-on-error semantics) so the
> fallback matches the C++.
>
> `historyStack.js` / `projectsStore.js` run as JS (their C++ counterparts exist
> but need a handle-based ABI rather than the flat numeric surface used by the
> rest); they are the natural candidates to route through wasm next.
