---
name: add-desktop-dialog
description: >-
  The file-by-file procedure for adding a dialog to the Stencil Qt desktop app — chrome and
  reveal helpers, CMake source lists, action and menu wiring, the canonical hotkey, the shared
  stylesheet, a headless test and the UI pins. Use when asked to add or restructure a desktop
  dialog, modal or settings page.
---

# Add a desktop dialog

Read `desktop/ARCHITECTURE.md` first; `.claude/rules/desktop-qt.md` has the Qt traps.

1. `desktop/src/dialogs/<feature>/<name>Dialog.{hpp,cpp}` — chrome from
   `desktop/src/support/modal/modalChrome.hpp`, reveal from
   `desktop/src/support/modal/modalReveal.hpp`. Measure per-state heights in `showEvent`, not
   from a constructor `sizeHint` (a hidden widget's hint is stale).
2. Add the sources to `desktop/cmake/sources/dialogs.cmake` (the dialogs' part of the GUI set
   that `desktop/cmake/StencilSources.cmake` includes); a new folder joins `STENCIL_GUI_DIRS` in
   `desktop/cmake/sources/dirs.cmake`, or its includes carry the prefix.
3. Create the action in `desktop/src/app/actions/ActionsBuilder.cpp` (its handler wired in
   `ActionsBuilderWiring.cpp`, its tip in `ActionsBuilderTips.cpp`) and place it in a menu in
   `desktop/src/app/actions/MenuBuilder.cpp` — builders only connect, the handler lives on
   the window or a part.
4. A shortcut goes in `browser/js/config/hotkeysConfig.json` — canonical — and reaches the
   desktop through the qrc alias.
5. Styling goes in the shared sheet in `desktop/src/support/theme/theme.cpp`, never
   `setStyleSheet` on the widget.
6. Add a headless test under `desktop/tests/`, then re-pin: record the renders on the
   pre-change tree with `STENCIL_UPDATE_UI_PINS=1 ctest --test-dir desktop/build -R uipins`,
   and run it plain after. Build with `-j 4`, never a bare `-j`.
