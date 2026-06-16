# Stencil — Desktop app (C++ / Qt)

The desktop front-end of Stencil: a C++17 + Qt 6 port that shares its pure logic
with the browser app. For the project overview see the
[repository README](../README.md).

## Dependencies

Per the project's dependency policy, only **two** third-party libraries are used:

| Purpose | Library | How it's provided |
|---|---|---|
| Desktop GUI | **Qt 6** (Widgets) | system package (e.g. `qt6-qtbase-devel`) |
| C++ unit tests | **Doctest** | single header fetched into `third_party/doctest.h` (not committed — see [Test](#test)) |

Everything else is the C++17 standard library. The **`core/` library is GUI-free
and STL-only** — it never includes Qt — so the same sources can later target
**WebAssembly** and back the browser app.

### Installing the toolchain

```bash
# Fedora
sudo dnf install -y gcc-c++ cmake qt6-qtbase-devel

# Debian / Ubuntu
sudo apt install -y g++ cmake qt6-base-dev
```

## Layout

```
core/                 # shared, GUI-free logic (STL only)
  models.hpp          # Point / Line value types
  geometry.{hpp,cpp}  # distToSegment            <- browser/js/utils.js
  color.{hpp,cpp}     # parseHex / hexToRgba     <- browser/js/utils.js
  pageMetrics.{hpp,cpp} # pixel <-> page (cm)    <- browser/js/core/drawingApp.js
  formulaParser.{hpp,cpp} # f(x,y) parser        <- browser/js/core/formulaEngine.js
  historyStack.{hpp,cpp}  # undo/redo            <- browser/js/core/historyStack.js
  projectsStore.{hpp,cpp} # registry + expiry    <- browser/js/core/projectsStore.js
  imageFilter.{hpp,cpp}   # bw/sepia/tint pixels <- browser/js/core/renderer.js
  zoomPan.{hpp,cpp}       # clamp / anchored zoom<- browser/js/core/zoomPan.js
  tooltipRows.{hpp,cpp}   # hover tooltip rows   <- browser/js/ui/tooltip.js
  wasmApi.cpp             # extern "C" ABI (WebAssembly) -> see WASM.md
gui/                  # Qt widgets (mirrors the browser UI)
  main.cpp            # entry point              <- browser/js/index.js
  mainWindow.{hpp,cpp}# window + toolbar + status<- browser/js/ui/layout.js, toolbar.js
  canvasWidget.{hpp,cpp} # QPainter rendering    <- browser/js/core/renderer.js, zoomPan.js
tests/                # Doctest suites (one per core module)
third_party/          # doctest.h — fetched on demand, git-ignored (see Test)
CMakeLists.txt
```

### Architecture parity with the browser app

The C++ app deliberately mirrors the JS module structure so the two read the same.
The `core/` modules are kept **behaviorally identical** to their JS counterparts and
are covered by the same test cases (ported from `browser/tests/`).

One deliberate divergence: the browser `formulaEngine.js` evaluates formulas with
`new Function(...)` (JavaScript `eval`). The C++ `formulaParser` is instead a real
**recursive-descent parser** for `+ - * / ** ( )` and a single variable — no `eval`,
no functions, division-by-zero / overflow reported as invalid. It is right-associative
for `**` and treats an empty expression as identity, matching the engine's
validate/apply contract. This parser is the intended shared implementation if/when the
core is compiled to WebAssembly for the browser.

## Build

```bash
# from this directory (desktop/)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

- `stencil_core`  — static library of the shared logic.
- `stencil_tests` — the Doctest executable (always built).
- `stencil_gui`   — the Qt desktop app (built only if Qt 6 is found; otherwise
  configuration prints a notice and skips it, so the core and tests still build
  on a machine without Qt).

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

CI builds these for all three platforms on every `v*` tag and attaches them to the
GitHub release (`.github/workflows/release.yml`); a manual `workflow_dispatch` run
produces the same packages as downloadable workflow artifacts without cutting a tag.

> On Qt < 6.3 the install step can't bundle Qt automatically — run the platform's
> `*deployqt` tool against the built app manually before packaging.

## Test

Unit tests use **Doctest** only — a single header at `third_party/doctest.h`
(pinned to **v2.4.11**). It is **not committed**: the CMake
configure step downloads it from the official upstream and verifies its SHA-256, so
on a fresh clone there is nothing to do — just configure and build.

If you'd rather fetch it yourself (e.g. offline-prep, no network at configure time),
run this from this directory before `cmake`:

```bash
# create the dir if missing, then download the pinned single header from the
# official doctest GitHub release tag (skips the download if already present)
mkdir -p third_party
[ -f third_party/doctest.h ] || curl -fsSL -o third_party/doctest.h \
  https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h

# wget equivalent:
# mkdir -p third_party && [ -f third_party/doctest.h ] || wget -qO third_party/doctest.h \
#   https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
```

Either way `stencil_tests` is always built (even without Qt). Each `core/` module
has a suite under `tests/`,
ported case-for-case from the browser app's `browser/tests/` plus extra coverage
for the new recursive-descent parser, plus the shared image-filter math and the
WebAssembly ABI surface (compiled natively — see [WASM.md](WASM.md)). Suite:
**88 cases / 255 assertions**.

```bash
# run the whole suite via ctest
ctest --test-dir build --output-on-failure

# or run the Doctest binary directly (supports filtering, e.g. by suite)
./build/stencil_tests
./build/stencil_tests --test-case="*power*"
```

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
| `--incognito` | Edit without saving. Honored only when a fresh image `--src` is opened — never for a saved `--project`. |
| `--layout <path\|url>` | A layout JSON applied once the `--src` image loads successfully (local file or URL). Ignored without `--src`. |
| `--projects` | Open the Projects window at launch. |
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

The desktop app mirrors the browser app's interaction surface:

- **Light / dark theme** (default light) — Ctrl+D or Settings; QSS derived from
  `browser/css/theme.css` tokens.
- **Top menu bar** (File / Edit / View / Project / Help) plus a toolbar, sharing the
  same actions.
- **Keyboard shortcuts** ported from `browser/js/config/hotkeysConfig.json`
  (embedded as a Qt resource) with **tooltips** showing label + shortcut.
- **Right-click context menu** on the canvas (New Line, Delete Last Point, Clear
  All, Deselect).
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
> styling panel, image filters, drag-and-drop, the fullscreen overlay layer, and
> clipboard copy/paste of the image + layout JSON.
