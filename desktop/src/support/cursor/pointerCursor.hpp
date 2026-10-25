#pragma once

// The app-wide cursors the browser gets from CSS; desktop twin of `cursor: pointer` and
// `cursor: not-allowed`.
class QApplication;

namespace stencil::gui {

  // A disabled widget takes no mouse events, so Qt never shows a cursor for it. This watches the
  // pointer application-wide instead (browser `cursor: not-allowed` on :disabled).
  void installDisabledCursor(QApplication* app);
  // Browser `cursor: pointer` on every button, checkbox or radio (label included), select and
  // context-menu item; install before the first window is built.
  void installPointerCursor(QApplication* app);

}
