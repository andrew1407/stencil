---
description: Qt 6 and macOS traps in the desktop app — sizing, stylesheets, hi-dpi, modals, and running its build and suites on this machine
paths:
  - "desktop/**"
---

# Desktop (Qt) traps

Each of these cost a debugging cycle, and most are invisible to `grab()` and the offscreen
platform the suites run on.

## Sizing

- **A hidden widget's `sizeHint()` is stale.** `updateGeometry()` is a no-op while hidden, so a
  constructor measures every state the same. Measure per-state heights in `showEvent` (call
  `layout()->activate()` between states) and `resize()` to the tallest; an explicit
  `setMinimumHeight()` overrides the layout's minimum and lets content squeeze.
- **A hint that spans two layout levels lags before first show**: a child's LayoutRequest is
  dropped while its parent is hidden. Call `child->updateGeometry()` before deriving a fixed size
  from a grandparent's `sizeHint()`.
- **A fixed-width top-level with wrapped labels opens too tall** — its minimum is computed at the
  narrowest width. Set an explicit minimum height and resize to `totalHeightForWidth(width())`
  after flushing `QEvent::LayoutRequest`.
- **Top and bottom dock areas lay docks out side by side.** The editor shell's top area already
  holds the hidden selected-line dock (`WindowAssembly::setupDocks`), so a plain
  `addDockWidget(Qt::TopDockWidgetArea, dock)` lands the newcomer beside it: a narrow
  right-aligned dock, not a full-width band. Pass `Qt::Vertical` as the third argument, or
  `splitDockWidget(…, Qt::Vertical)` once the neighbour is shown (`MainWindow::onSelectionChanged`
  for the Image Size dock) — a split against a hidden dock does not register.

## Stylesheets

- **QSS `min-height` is a button's `minimumSizeHint`.** `min-height:0` lets a squeezed dialog
  crush buttons to their padding; keep a real one (the font's line box).
- **Style through the shared ID-selector sheet**, reissued on state change, never
  `widget->setStyleSheet(...)` on one instance: a local sheet can make a child word-wrapped
  `QLabel` a couple of pixels taller than its identically styled sibling.
- **Under an app stylesheet `qApp->style()` is Qt's private `QStyleSheetStyle`**, so a test
  cannot see the base style through `QProxyStyle`; assert `installedStyleKey()`.
- **Rich-text `<hr>` paints in the text colour**; only `<hr style="background-color:…">`
  recolours it.

## Hi-dpi, painting, motion

- **The offscreen platform is devicePixelRatio 1.** A 1x `QPixmap` icon draws half-size on
  Retina, and a live `QToolButton` icon can draw half-size even when `grab()` shows it right —
  paint such a mark through a `QPainter` overlay. Probe hi-dpi with `QT_SCALE_FACTOR=2` or on
  the real cocoa platform.
- **Paint many small motes as cached sprites** (`support::MoteSprites`), never `drawEllipse` in
  a loop — an order of magnitude cheaper; batched `QPainterPath`s are slower still.
  Frame-rate motion steps on the one display-synced clock, `support/vsync/FramePulse.hpp`
  (`FrameTicker` where a `QTimer` would go), never its own timer: two clocks paint a frame
  twice, and a 16 ms timer beats against the display. `AnimationDriver` puts Qt's animations on it.
  A `QWindow::requestUpdate()` on a widget's window is a full `repaint()` — the clock beats a
  hidden window of its own.
- **A genie flight reads its target once**, as the browser's `genieIn` / `landingOf` do: where the
  icon stands before an open, where a close lands it. A target re-read every frame (`fly`'s
  `follow`, a chased pointer) rides the sliding layout and judders the funnel's mouth a pixel a
  frame: the fullscreen top and side menus and the docked chat all read rough beside the browser for it.
- **The raster engine pays per scanline span**: a genie flowing sideways cuts 3 px-wide,
  full-height bands, ~7× the cost of the same picture flowing down. `support/genie/stripPaint.hpp`
  merges bands and paints such a flow transposed; profile with `sample <pid>` on cocoa, not offscreen.
- **A window-wide slide repaints the whole window every frame** (~5 ms full-screen @2x): at 120 Hz that
  drops frames unevenly. Qt's dock animator made a zero-length animation per relayout and repainted the
  separators a paint late (the app style now answers `SH_Widget_Animation_Duration` 0 for a QMainWindow's
  children); a parent fills under any child Qt does not know is opaque (`WA_OpaquePaintEvent` + its own
  `PE_Widget`, as the editor's ground); and `startExtentSlide` puts the pulse on half rate when the paint
  outruns half a frame. Measure with `QWidget::render` into a 2x image — on screen a covered window paints nothing.
- **A dock slide keeps the dock `isVisible()` throughout**, and Qt clamps negative contents margins: the
  chat's 10 px page gap slides by setting the window's separator to 0 (`[chatGapless]`) while covered.

## macOS windows

- **An application-modal dialog's outside press never reaches an event filter** (AppKit drops
  it inside `runModalForWindow`). Dismiss-on-outside-click needs a transparent `Qt::Tool` child
  window below the dialog; the offscreen test does not prove a real click works.
- **An application-modal dialog starves Qt's video output**: `QMediaPlayer` + `QVideoSink` play
  to the end with no frame inside its Cocoa session, so a dialog that plays video is window-modal
  (`support::PLAYS_VIDEO_PROPERTY`). Offscreen never gets a frame either: prove it on screen.
- **A window-modal window with a transient parent opens as an AppKit sheet**, pinned to the
  parent's top edge: `move()` and every landing or flight are ignored. Show it parentless and
  restore the transient parent right after (`showUnsheeted`, `PopoverGesturesSideBySide.cpp`).
- **Veil a top-level (`setWindowOpacity(0)`) before it maps**, or it flashes before its reveal.
- **Mapping a window flushes the others**: a layout pinned only to be photographed (a pour's
  `pin(full)` + `grab`) is put back before a ghost window shows and repaints, or that layout
  blinks on screen for a frame (`DockChrome::pourDockedChat`).
- **A layout that slides a widget out from under a still pointer leaves it hovered**: no Leave
  comes. The motion's end calls `support::dropStaleHover` on the control that was pressed.
- **A closed dialog can leave a stale cursor** — `setWindowCursor` skips an unchanged shape; the
  fix re-applies it through a different shape (`support/modal/hoverResync.cpp`).
- **`QWidget::winId()` nativizes every sibling** — take an NSView through
  `windowHandle()->winId()` on a shown window.
- **Cursor warps (`QCursor::setPos`, `QTest::mouseMove`) make no Enter/Leave/ToolTip events on
  cocoa**, and this terminal has no Accessibility rights: drive hover state directly and send a
  synthetic `QHelpEvent`; a test that warps must check `QCursor::pos()` and skip.
- **No Screen Recording rights either**: `screencapture` fails, so take frames with `win.grab()`;
  an overlay that escaped to its own top-level is grabbed directly, after `windowHandle()->isExposed()`.

## Building and testing here

- **Never a bare `-j`**: it is unbounded make over hundreds of Qt units and freezes the machine.
  Build with `-j 4`, never beside another native build; run ctest at `-j 2`.
- **Run a test binary through ctest, or with `QT_QPA_PLATFORM=offscreen`** (plus
  `STENCIL_NO_ANIM=1` for the GUI areas). Run bare, it opens real focused windows that swallow
  live keystrokes, and a native `QMenu::exec` can hang until the watchdog aborts.
- **QtTest stops printing `qWarning` after its cap** — pass `-maxwarnings 0` when a probe seems
  never to run.
- **An incremental build shows warnings only for recompiled units.** To list the tree's warnings,
  configure a fresh build dir in the scratchpad and compile it once.
- **GUI flakes**: a single failing GUI case under load proves nothing — re-run it alone on a quiet
  machine, and `rm -rf desktop/build/test-state` first (persisted settings decide some
  animations). Some cases are order-dependent: re-run the pair, not the one. A green run can be a
  lucky persisted state; re-check by name, twice.
- **A headless suite must end `return failures ? 1 : 0;`** (`grep -L 'return failures'` over
  `tests/**/*.headless.cpp` lists offenders) — a bare `return 0` hides its FAILs
  from ctest.
- **The renders under `desktop/tests/pins/<platform>/` are a local, gitignored baseline**:
  record on the pre-change tree (`STENCIL_UPDATE_UI_PINS=1 ctest --test-dir desktop/build -R
  uipins`), compare after; never commit a render, never record on Linux to fix the CI skip.
