# Stencil — Desktop app (C++ / Qt)

The desktop front-end of Stencil: a C++17 + Qt 6 app that shares its logic with the
browser app through the C++ [`core/`](../core/). For the project overview see the
[repository README](../README.md); for how the app is structured,
[`ARCHITECTURE.md`](ARCHITECTURE.md). Screenshots and clips, scenario by scenario:
[`usecases/docs/desktop/USECASES.md`](../usecases/docs/desktop/USECASES.md).

## Build

Needs a C++17 compiler, CMake and **Qt 6** (Widgets, Network, Multimedia):

```bash
# Fedora
sudo dnf install -y gcc-c++ cmake qt6-qtbase-devel
# Debian / Ubuntu
sudo apt install -y g++ cmake qt6-base-dev
```

```bash
# from this directory (desktop/)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/stencil
```

If Qt 6 is not found, configuration prints a notice and skips the app so the core still
builds. This dev build keeps runtime state (settings, projects, the session autosave) in a
repo-local `desktop/.stencil/` directory and links Qt dynamically — it is not a
distributable binary; see [Release packaging](#release-packaging).

On macOS, system notifications need a signed app; an unsigned build shows them in the app
instead. Sign each build with a certificate from your keychain (Xcode → Settings → Accounts →
Manage Certificates → Apple Development):

```bash
security find-identity -v -p codesigning   # the name in quotes is the identity
cmake -S . -B build -DSTENCIL_CODESIGN_IDENTITY="Apple Development: you@example.com (TEAMID)"
```

## Test

```bash
ctest --test-dir build --output-on-failure   # runs every headless test (needs Qt)
ctest --test-dir build -R <name>             # one target
```

The desktop registers Qt offscreen CTest targets: headless suites per concern, the
lints, the UI pins, and `stencil_mainwindow_<area>_gui`
— QtTest end-to-end binaries that drive the real `MainWindow` (the desktop's e2e; the repo's
Playwright `e2e/` harness does not cover it). The core's own Doctest suite is built and run
from `core/`. `-DSTENCIL_DOCS_CAPTURE=ON` adds `stencil_docs_capture`, the opt-in binary
behind the screenshots in `usecases/docs/desktop/` (run through `usecases/capture-runner/desktop.mjs`).

## Using the app

Open an image, left-click to add polyline points, **New Line** (`Alt+S`) to finish the
current line, right-click (or `Shift+F10`) for the canvas context menu. The status bar shows
the cursor's pixel and page (cm) coordinates. Hold-to-draw, rectangles, per-line style,
filters (none / B&W / sepia / invert / contour / tint — `Alt+B` cycles them), crop, rotate,
zoom, undo/redo and the ISO A/B/C page formats (searchable toolbar selector, plus a custom
size) work as in the browser app, with the same keyboard shortcuts.

- **Light / dark theme** — `Ctrl+D` or Settings; a per-project accent colour can be set from
  the swatch next to the project name.
- **Dialogs** — **Visuals & Settings** (`Alt+V`: theme, motion, menu-bar placement,
  autosave, defaults, page size, AI assistant), **Projects** (save / open / delete, one-week
  expiry), **Controls & Shortcuts Info** (`Alt+H`), and the **Keyboard Shortcuts** editor
  (`Alt+K`: click a combo and press the new chord).
- **Motion** — Settings → Motion: *Drawing animation* on/off and *Interface animation*
  (Dust, Water, Fire, Sliding, None). `STENCIL_NO_ANIM=1` overrides the setting.
- **Notifications** — Settings → Notifications: *In the app* (the corner toasts, the default) or
  *System notifications* (the OS notification centre, through a tray icon shown while chosen).
  Where the OS offers none, the toasts stay.
- **Menu bar** — *Use the system menu bar* (default on) puts the menus where the platform
  does; turn it off to keep them in the window (takes effect on restart).
- **Project actions** — `Ctrl+Alt+R` removes the current project from the editor,
  `Ctrl+Alt+N` renames it.
- **Autosave** — the in-progress drawing is restored on next launch.

### Scripts (`.stc`)

**Data ▸ Stencil Script…** (`Alt+Shift+S`) opens an editor for a stencil script — crop,
filter, draw, save and undo written as `@` directives, coloured as you type. **Run** applies
it to the open project; **Open…** loads a `.stc` from disk, **Save…** writes one, and
**Copy** puts the text on the clipboard. Mistakes are only reported once you press Run: the
line is named under the editor and underlined in place, and a script with any error runs
nothing. Dropping a `.stc` on the window runs it straight away, or fills the editor when the
script window is already open. The language is the same one the CLI's `--script` takes, and
is written up in [`contracts/stc/stc-contract.md`](../contracts/stc/stc-contract.md).

### Project files (`.stencil`)

**Data ▸ Open Project…** (`Ctrl+Shift+F`) and **Save Project As…** (`Ctrl+Shift+S`) read and
write the portable `.stencil` file — the original image, the layout, project metadata and the
current colour theme in one document that opens on any Stencil surface. A `.stencil`
double-click, drag or file argument opens it the same way once the app is installed.

### Collaboration server

**🖧 Servers…** on the toolbar (also **Project ▸ Servers…**) connects to one or more
[collaboration servers](../server/README.md). A connected row offers **Invite**, which
copies an `<server-url>#token=<token>` link; pasting one into the URL field adopts its token.
Server projects appear in the **Projects** dialog as golden rows; **Open** downloads the
original image + layout, and **New Project / Save** offers this computer or a server as the
target (a version conflict surfaces as "edited elsewhere"). The desktop talks REST only;
live co-editing over the TCP channel is not implemented here.

### AI assistant

The **✦ Assistant** toolbar button (**View ▸ Assistant**, `Alt+G`) toggles a chat dock that
can sit on any window edge or float; the canvas right-click menu carries the same
conversation as an **Assistant ▸** submenu. Prompts go to the provider configured in the
dock's **…** menu ▸ Settings (`Alt+Shift+G`, or **Settings ▸ AI assistant**) and come back as
a validated op-plan executed through the same appliers as the toolbar; variants open as their
own project entries. Attach images or videos (frames are extracted with Qt Multimedia; the
video itself is never sent). The model may also open or save local files, but only at a path
you yourself wrote in the conversation. Setup guide:
[root README → AI assistant](../README.md#ai-assistant--setting-up-a-model).

## Launch options

```bash
./build/stencil [options] [<file> | stencil://…]
```

| Flag | Description |
| --- | --- |
| `--theme <dark\|light>` | Set (and persist) the theme for this launch. |
| `--project <name>` | Open a saved project by name (case-insensitive). Takes precedence over `--src`. |
| `--src <path\|url>` | Open a local image, fetch a remote image URL, or grab a frame from a video file / direct media URL. |
| `--frame <n>` | The 0-based video frame to open (default: first). |
| `--incognito` | Edit without saving (alone: a blank incognito editor). |
| `--layout <path\|url>` | A layout JSON applied once the `--src` image loads. |
| `--projects` | Open the Projects window at launch. |
| `<file>` | A bare image / video / layout-JSON / `.stencil` path — what an OS file association passes. |
| `stencil://…` | A deep link (the argv form a Linux scheme handler passes). |
| `--help` | Show the full option list. |

```bash
./build/stencil --src ~/Pictures/floorplan.png --theme light
./build/stencil --src ~/clips/walkthrough.mp4 --frame 120 --incognito
./build/stencil --src floorplan.png --layout https://example.com/layout.json
./build/stencil --project "Kitchen remodel"
```

Remote images and video frames are adopted in-memory, so they carry no on-disk path. Video
support reads direct media files/URLs, not streaming page links.

### Deep links (`stencil://`) and "Open In…"

The app registers the `stencil://` scheme (macOS via the bundle plist, Linux via the
`.desktop` file; Windows registration is not shipped). A link opens a fresh window:

```
stencil://open?server=<origin|host[:port]>&id=<projectId>[&version=<n>][&incognito=1]
stencil://open?src=<http(s) url|data:…>[&layout=<inline JSON>][&frame=<n>][&incognito=1]
```

A `server`+`id` link connects like a fresh client — reusing a saved token for the origin,
else minting one (no token ever rides a link) — and asks for confirmation before adding a
server the machine has never used. A `src` link opens an http(s) or `data:` URL, never a
local path.

Outbound, **Project ▸ Open In…** (also on the toolbar) hands the current session to the
**browser app** (base URL in Settings → "Browser app URL") or the **Telegram bot** (server
projects only; set Settings → "Telegram bot" to your bot's username).

### OS-shell integration

- **Drag-and-drop** an image, video, layout `*.json`, project `*.stencil` or script `*.stc`
  onto the window.
- **File associations / "Open With"** — macOS (Finder, drag-onto-Dock), Linux (`.desktop`
  `MimeType=`), Windows (register in your installer) all launch or signal Stencil with the
  file.
- **App-icon menu** — the macOS Dock menu and the Linux launcher actions offer *New
  Incognito Editor*, *Open Projects…* (and, on macOS, the recent projects).

These take effect once the app is **installed** (`cmake --install build`, then
`update-desktop-database` on Linux / LaunchServices registration on macOS), not when running
straight from `build/`.

## Release packaging

A distributable build stores state in the per-user config dir and bundles Qt alongside the
app:

```bash
# from this directory (desktop/)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTENCIL_DEV_STATE_DIR=OFF
cmake --build build --config Release -j
cpack --config build/CPackConfig.cmake -B build/dist
```

`cpack` runs Qt's deploy helper (`macdeployqt` / `windeployqt`; best-effort on Linux, Qt ≥
6.3 — on older Qt run the platform's `*deployqt` by hand first) and wraps the result:

| Platform | Package | Form |
|---|---|---|
| macOS | `stencil-<ver>-Darwin-<arch>.dmg` | `stencil.app` bundle |
| Windows | `stencil-<ver>-Windows-<arch>.zip` | `bin/stencil.exe` + Qt DLLs |
| Linux | `stencil-<ver>-Linux-<arch>.tar.gz` | `bin/` + `.desktop` entry & icon |

Packaging registers the `.stencil` file type and the `stencil://` scheme with the OS. The
macOS bundle is unsigned and warns on first launch; its icon is generated at configure time
from `../browser/favicon.svg` (needs `sips` + `iconutil`, otherwise the build is iconless).
CI builds all three packages on every `v*` tag and attaches them to the GitHub release
(`.github/workflows/desktop-packages.yml`); a manual run produces them as workflow artifacts.
