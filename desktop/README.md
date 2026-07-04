# Stencil — Desktop app (C++ / Qt)

The desktop front-end of Stencil: a C++17 + Qt 6 port that shares its pure logic
with the browser app. For the project overview see the
[repository README](../README.md).

## Architecture

```mermaid
graph TD
    CORE["<b>core/</b> — shared C++ logic<br/><i>add_subdirectory(../core), STL-only</i>"]
    subgraph APP["desktop/ — C++17 + Qt 6"]
      MAIN["app/ — main · MainWindow · launchOptions · selectionPanel"]
      CANVAS["canvas/ — CanvasWidget (QPainter) + tooltip"]
      DLG["dialogs/ — settings · projects · blank · crop · connect · …"]
      IO["io/ — fileStore (persistence) · mediaLoader"]
      NET["net/ — serverClient (REST)"]
      SUP["support/ — theme · notifications · guiHelpers"]
    end
    SRV["Collaboration server"]

    CORE -->|"linked static lib · pixel / geometry / page math"| CANVAS
    CORE --> DLG
    MAIN --> CANVAS
    MAIN --> DLG
    NET -.->|"connect · REST only (QNetworkAccessManager, no WS)"| SRV

    click CORE "../core/README.md#architecture" "Shared core architecture"
    click SRV "../server/README.md#architecture" "Collaboration server architecture"
```

> Click a node to open that surface's own architecture diagram, or see the whole-system
> view in the [repository README](../README.md#architecture).

## Dependencies

Per the project's dependency policy, only **two** third-party libraries are used:

| Purpose | Library | How it's provided |
|---|---|---|
| Desktop GUI | **Qt 6** (Widgets, Network, Multimedia) | system package (e.g. `qt6-qtbase-devel`) |
| C++ unit tests | **Doctest** | single header fetched into `../core/third_party/doctest.h` (not committed) |

Everything else is the C++17 standard library. The shared logic lives in the top-level
[`../core/`](../core/) library — **GUI-free and STL-only** (it never includes Qt) — which
this app pulls in via `add_subdirectory(../core)`. The same sources also compile to
**WebAssembly** for the browser app and into the **Zig CLI** (`../cli/`).

### Installing the toolchain

```bash
# Fedora
sudo dnf install -y gcc-c++ cmake qt6-qtbase-devel

# Debian / Ubuntu
sudo apt install -y g++ cmake qt6-base-dev
```

## Layout

The shared logic lives in the sibling [`../core/`](../core/) library (see its
[WASM.md](../core/WASM.md) and the repo README for the module list). This directory holds
only the Qt GUI and its integration test:

```
src/                  # Qt GUI, grouped by role (headers included bare across groups)
  app/                # main.cpp, mainWindow, launchOptions, selectionPanel
  canvas/             # canvasWidget (QPainter rendering) + canvasTooltip
  dialogs/            # settings / projects / blank / links / crop / info / shortcuts / connect
  io/                 # fileStore (persistence) + mediaLoader (image/video --src)
  net/                # serverClient: REST + connection manager for the collaboration server
  support/            # theme, notifications, guiHelpers
tests/                # Qt headless integration tests (crop + image fixture)
  fixtures/           # sample.png used by the image test
resources/  packaging/
CMakeLists.txt        # builds stencil_gui; pulls the core via add_subdirectory(../core)
```

### Collaboration server

The **🖧 Servers…** button on the main toolbar (mirroring the browser's connect icon) — also
**Project ▸ 🖧 Servers…** — opens the connect dialog (`dialogs/connectDialog`), which connects to,
reconnects, and disconnects one or more [Stencil collaboration servers](../server/README.md)
through `net/serverClient` (a `QNetworkAccessManager` REST client + a multi-connection
`ConnectionManager`). The desktop talks to the server over Qt Network (REST) only — **no
`QWebSocket` / third-party WebSocket dependency**.

Connected servers expose their stored projects in the **Projects** dialog as a **golden band
(gold fill + bold gold text) with a 🖧 marker**, listed alongside local projects and refreshed live by a short
periodic re-list while the dialog is open (the REST stand-in for the browser's WebSocket
project-event feed). **Open** on a golden row downloads the project's original image + layout
and loads them into the editor, linking the session to `{address, remoteId, version}`. When a
server is connected, **New Project / Save** offers a target — this computer or a server;
saving a server-linked session does a version-guarded `PUT` of name + layout (a 409 surfaces
an "edited elsewhere" message) and uploads the rendered result. Live *co-editing* over a TCP
edit transport is **not** implemented on the desktop.

### Architecture parity with the browser app

The C++ app deliberately mirrors the JS module structure so the two read the same, and
its shared logic *is* the same code the browser runs (compiled to WebAssembly). The
behavioral-parity contract and the core's design principles — including the one deliberate
divergence, the eval-free recursive-descent `formulaParser` that replaces the browser's
`new Function(...)` — are documented with the core: see
[`../core/README.md`](../core/README.md).

Like the browser app, each project carries an optional **accent colour** that paints its
name everywhere it appears — the toolbar project-name field, the window title field, and
the rows in the Projects window. Set or clear it from the swatch button next to the
project name or the per-row "Set colour…" / "Clear colour" actions in the Projects window
(empty = a neutral muted grey, readable on light and dark). The colour is saved with the project and, for a
server-backed project, pushed to the collaboration server so every connected client
(browser/desktop/CLI) re-renders the name in it.

## Build

```bash
# from this directory (desktop/)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

- `stencil_core`  — static library of the shared logic (defined in `../core/`, pulled in
  here via `add_subdirectory`; its own Doctest suite is turned **off** for the desktop
  build and run by the dedicated core build instead).
- `stencil_gui`   — the Qt desktop app (built only if Qt 6 is found; otherwise
  configuration prints a notice and skips it, so the core still builds on a machine
  without Qt).
- `stencil_crop_headless` — a Qt offscreen integration test of the crop canvas (ctest).

To build and run the **core's** unit suite directly: `cmake -S ../core -B ../core/build &&
ctest --test-dir ../core/build`.

The plain build above bakes a repo-local `desktop/.stencil` runtime-state dir
(handy for development) and links Qt dynamically — it is **not** a distributable
binary. For that, see below.

## Release packaging

A distributable build differs from the dev build in two ways: runtime state goes
to the **per-user config dir** instead of the repo (`-DSTENCIL_DEV_STATE_DIR=OFF`),
and Qt is **bundled alongside the app** so it runs on a machine without Qt
installed. Both are handled by the install + CPack flow:

```bash
# from this directory (desktop/)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTENCIL_DEV_STATE_DIR=OFF
cmake --build build --config Release -j
cpack --config build/CPackConfig.cmake -B build/dist
```

`cpack` runs Qt's deploy helper (`macdeployqt` / `windeployqt`, best-effort on
Linux — Qt ≥ 6.3) to copy the Qt libraries and plugins next to the app, then wraps
the result into one self-contained package in `build/dist/`:

| Platform | Package | Form |
|---|---|---|
| macOS | `stencil-<ver>-Darwin-<arch>.dmg` | `Stencil.app` bundle |
| Windows | `stencil-<ver>-Windows-<arch>.zip` | `bin/stencil_gui.exe` + Qt DLLs |
| Linux | `stencil-<ver>-Linux-<arch>.tar.gz` | `bin/` + `.desktop` entry & icon |

The shipped binary is still `stencil_gui` — packaging only changes how it's laid
out and where it stores state, not what it is. On macOS, signing/notarization is
out of scope here; an unsigned `.dmg` warns on first launch.

**App icon.** The macOS bundle's icon is generated at configure time by
`packaging/make-icns.sh`, which rasterises `../browser/favicon.svg` (the shared
single-source artwork) into a multi-resolution `stencil.icns` via `sips` +
`iconutil` and lands it in `Contents/Resources/` with `CFBundleIconFile` wired up.
Nothing binary is committed — the icon stays derived from the SVG. If `sips`/
`iconutil` are missing CMake warns and builds iconless.
This is a flat `.icns`, so it renders full-colour and does **not** follow the
macOS 26 Dock *tint* appearance — system tinting only applies to layered,
appearance-aware icons authored as an Icon Composer `.icon` and compiled with
`actool`, which needs a full Xcode install (the Command Line Tools alone can't
build it). Adding a tintable layered icon is a future follow-up.

CI builds these for all three platforms on every `v*` tag and attaches them to the
GitHub release (`.github/workflows/release.yml`); a manual `workflow_dispatch` run
produces the same packages as downloadable workflow artifacts without cutting a tag.

> On Qt < 6.3 the install step can't bundle Qt automatically — run the platform's
> `*deployqt` tool against the built app manually before packaging.

## Test

The **core's** Doctest suite (one suite per core module, the WebAssembly ABI, and the new
CLI image-pipeline modules) lives with the core and is built/run there:

```bash
cmake -S ../core -B ../core/build -DCMAKE_BUILD_TYPE=Release
cmake --build ../core/build -j
ctest --test-dir ../core/build --output-on-failure   # or ../core/build/stencil_tests
```

Doctest is a single header (pinned **v2.4.11**), fetched into `../core/third_party/doctest.h`
at configure time with SHA-256 verification — nothing to commit or install.

The **desktop** build registers several Qt offscreen CTest cases of its own. Most exercise a
component in isolation; `stencil_mainwindow_gui` is a full GUI **end-to-end** built with the
**Qt Test framework** — it drives the real `MainWindow`:

- `stencil_mainwindow_gui` — GUI e2e (QtTest): loads an image via the OS-open path, then
  drives the **real, shared QActions** (menu bar / toolbar / context menu reuse the same
  objects) and sends real mouse clicks to the live canvas, asserting on observable widget
  state. Five flows: action-enablement on load, a **Rotate** round-trip (asserting the
  quarter-turn W↔H dimension swap, not just the counter), **draw → New Line → Undo → Redo**
  (exact point count + history availability), a **filter** action landing on the canvas
  (exclusive group), and **Clear All Lines** emptying it. It verifies MainWindow's *action
  wiring* end-to-end; the underlying canvas *logic* is covered by the isolated headless
  suites below.
- `stencil_crop_headless` — crop canvas integration (`CanvasWidget` + the core crop geometry).
- `stencil_image_headless` — loads a real PNG from `tests/fixtures/` and runs it through the
  load → crop → core image-filter path (the desktop analogue of the CLI's fixture tests).
- plus `stencil_holddraw_headless`, `stencil_layout_headless`, `stencil_projectcolor_headless`,
  `stencil_deeplink_headless`, and `stencil_livefeed_headless`.

```bash
ctest --test-dir build --output-on-failure   # runs every headless test (needs Qt)
```

> **Where desktop e2e lives.** This QtTest target is the desktop app's only end-to-end test;
> the repo's cross-surface [`e2e/`](../e2e/) Playwright harness deliberately does **not** cover
> desktop (a native Qt binary is not a wire-protocol surface it can drive) — it exercises the
> browser app, the Chrome extension, and the Go server binary instead. The two are complementary.

## Run the GUI

```bash
./build/stencil_gui
```

Open an image, left-click to add polyline points, **New Line** (Alt+S) to finish
the current line, and right-click for the canvas context menu. The status bar shows
the cursor's pixel and page (cm) coordinates, computed by the shared `core` exactly
as the browser app does.

### Launch options (CLI)

The executable accepts flags that pre-open content at startup — the desktop
counterpart of the browser app's URL deep-links (`#stencil=` / `?open=`):

```bash
./build/stencil_gui [options]
```

| Flag | Description |
| --- | --- |
| `--theme <dark\|light>` | Set (and persist as) the default theme for this launch, overriding the saved/system choice. |
| `--project <name>` | Open an existing, editable saved project by name (case-insensitive). Takes precedence over `--src`. |
| `--src <path\|url>` | Open an image by local path, fetch and open a remote image URL, or grab a frame from a video file / direct media URL. |
| `--frame <n>` | The 0-based video frame to open (default: first frame; ignored for still images). |
| `--incognito` | Edit without saving. Honored unless a saved `--project` is being opened — so `--incognito` alone starts a blank incognito editor. |
| `--layout <path\|url>` | A layout JSON applied once the `--src` image loads successfully (local file or URL). Ignored without an image. |
| `--projects` | Open the Projects window at launch. |
| `<file>` (positional) | A bare image / video / layout-JSON path — the form an OS file-association or "Open With" passes. `*.json` is applied as a layout, anything else opened as an image/video. Lower priority than `--src`. |
| `stencil://…` (positional) | A `stencil://open?…` deep link (the argv form a Linux scheme handler passes via `%u`). See **Deep links** below. |
| `--help` | Show the full option list. |

Examples:

```bash
# Force dark mode for this launch
./build/stencil_gui --theme dark

# Open a local image, starting in light mode
./build/stencil_gui --src ~/Pictures/floorplan.png --theme light

# Fetch and open a remote image
./build/stencil_gui --src https://example.com/diagram.jpg

# Grab the 120th frame of a video file and edit it without saving
./build/stencil_gui --src ~/clips/walkthrough.mp4 --frame 120 --incognito

# Open an image and immediately apply a saved layout (local file or URL)
./build/stencil_gui --src floorplan.png --layout floorplan-layout.json
./build/stencil_gui --src floorplan.png --layout https://example.com/layout.json

# Reopen an existing saved project by name
./build/stencil_gui --project "Kitchen remodel"

# Launch straight into the Projects window
./build/stencil_gui --projects
```

The image / URL / video and layout resolution runs asynchronously on the event
loop after the window appears; a toast reports success or failure. Remote images
and video frames are adopted in-memory (like a clipboard paste), so they carry no
on-disk path; a local image `--src` keeps its path for session / project saves.
Video support reads **direct** media files/URLs (it does not resolve streaming
*page* links such as a YouTube watch URL).

### Deep links (`stencil://`) and "Open In…"

The desktop app registers the **`stencil://` URL scheme** (macOS via
`CFBundleURLTypes` in the bundle plist, Linux via `x-scheme-handler/stencil` in the
`.desktop` file; Windows registration is not shipped yet). Opening a link launches a
fresh window — the same "separate client" a user would open manually:

```
stencil://open?server=<origin|host[:port]>&id=<projectId>[&version=<n>][&incognito=1]
stencil://open?src=<http(s) url|data:…>[&layout=<inline JSON>][&frame=<n>][&incognito=1]
```

A `server`+`id` link connects to that collaboration server like a fresh client —
reusing a saved token for the origin, else minting one via `POST /auth/token` (no
token ever rides a link) — and opens the project; with `incognito=1` it opens an
unlinked incognito copy (nothing pushed back). A `src` link opens an image (http(s)
URL or inline `data:` URL — never a local path; links are remotely clickable) and
applies the inline `layout` JSON once loaded. Connecting to a server the machine has
never used first asks for confirmation, so a drive-by link can't silently add one.

Outbound, **Project ▸ Open In…** (also on the toolbar) mirrors the current session
the other way: into the **browser app** (the `#stencil=` fragment; base URL set in
Settings → "Browser app URL") or the **Telegram bot** (server projects only; set
Settings → "Telegram bot" to your bot's username). The browser's `launch.html`
bounce page carries bot→desktop links, since chat apps only linkify http(s).

### OS-shell integration

The same open paths are wired into the desktop shells:

- **Drag-and-drop** — drop an image, video, or layout `*.json` onto the window to
  open / apply it (Photoshop-style). Cross-platform.
- **File associations / "Open With"** — opening a declared file type launches (or,
  on macOS, signals a running) Stencil with that file:
  - **macOS** — a `QFileOpenEvent` (Finder double-click, drag-onto-Dock, "Open
    With") routed to the open window; the bundle declares image / movie / JSON
    document types in its `Info.plist`. Events arriving during launch are buffered
    until the window is ready.
  - **Linux** — the `.desktop` file declares `MimeType=` and opens the file via the
    `%f` positional argument.
  - **Windows** — registering a file association (in your installer) makes a
    double-click launch the app with the file as a positional argument, which the
    same code path opens.
- **App-icon menu** — right-click the icon for quick actions:
  - **macOS Dock menu** — *New Incognito Editor*, *Open Projects…*, and the most
    recently updated projects (each opens in its own window). Set via
    `QMenu::setAsDockMenu()`.
  - **Linux launcher actions** — *New Incognito Editor* and *Open Projects* via the
    `.desktop` `Actions=` entries (static; the freedesktop spec has no dynamic
    "recent" list).
  - **Windows Jump List** — *not implemented.* Qt 6 dropped the `QtWinExtras` jump-
    list API, so this needs native Win32 (`ICustomDestinationList`) code; the Dock /
    launcher equivalents above cover macOS and Linux.

> **Install-time caveat:** file associations, the macOS Dock document-type hooks,
> and the Linux launcher actions only take effect once the app is **installed**
> (`cmake --install build`, then `update-desktop-database` on Linux / LaunchServices
> registration on macOS) — not when running the binary straight from `build/`.
> Drag-and-drop onto the window and the CLI flags work regardless.

The desktop app mirrors the browser app's interaction surface:

- **Light / dark theme** (default light) — Ctrl+D or Settings; QSS derived from
  `browser/css/theme.css` tokens.
- **Top menu bar** (File / Edit / View / Project / Help) plus a toolbar, sharing the
  same actions.
- **Keyboard shortcuts** ported from `browser/js/config/hotkeysConfig.json`
  (embedded as a Qt resource) with **tooltips** showing label + shortcut.
- **Right-click context menu** on the canvas (New Line, Delete Last Point, Clear
  All, Deselect).
- **Page formats** — the toolbar page selector offers the full ISO 216/269
  series (A0–A10, B0–B10, C0–C10) plus a custom W×H size. Every option shows its
  physical size in the active display unit (cm/in), and the toolbar combo is
  searchable: type to filter the list (e.g. `b5`), case-insensitively. The same
  option list backs the Settings dialog and the Image Links quick-crop picker.
- **Image filters** — none / B&W / sepia / invert / contour / custom duotone
  tint, from the Style toolbar row, the canvas context menu, or the Alt+B
  cycle (none → B&W → sepia → invert → contour → tint). Contour runs the
  core's Sobel edge detection (dark edges on white); every mode produces the
  same pixels as the browser app by construction (shared `core/` math).
- **Selection panel** dock — the active line's points and live measurements (point
  count, segment count, total length).
- **Settings** dialog (theme, autosave, show points/lines, default visuals, page
  size), a **Projects** dialog (save / open / delete, with the `core/projectsStore`
  one-week expiry sweep), and an **Info & Shortcuts** dialog rendered from the shared
  config JSON.
- **Toast notifications** and **autosave**: the in-progress drawing (points, page
  format, zoom, image path) is autosaved to a gitignored temp config
  (`desktop/.stencil/session.autosave`) and **restored on next launch**; settings and
  projects live alongside it in `desktop/.stencil/` (path baked via the
  `STENCIL_STATE_DIR` build define; the directory is in `.gitignore`).

> Out of scope: rectangle/area drawing, the editable coordinate table, the per-line
> styling panel, drag-and-drop, the fullscreen overlay layer, and
> clipboard copy/paste of the image + layout JSON.
