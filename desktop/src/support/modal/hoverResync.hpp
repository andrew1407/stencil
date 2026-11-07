#pragma once
// Hover after a modal loop: Qt keeps the hover it recorded before the dialog or popover ran, and
// macOS keeps the closed window's cursor on screen, so a still pointer wore a stale hand and the
// first widget it reached never heard an Enter. Browser twin: none — the DOM re-hit-tests itself.
#include <QTimer>
#include <QWidget>

namespace stencil::support {

  // Back in front a turn later, only if the window lost its place: the system file panel can hand the key
  // window to the main window as it closes (user report); a raise every time blinked it.
  inline void reclaimFront(QWidget* w) {
    QTimer::singleShot(0, w, [w] {
      if (w->isActiveWindow()) return;
      w->raise();
      w->activateWindow();
    });
  }

  // Enters the widget under the pointer when Qt has it un-hovered, and puts its cursor back on.
  void resyncHover(QWidget* window);
  // Un-hovers `w` when Qt still holds it under a pointer that is no longer over it: the layout slid it out
  // from under a still pointer, or a window mapped over it, and its Leave never came.
  void dropStaleHover(QWidget* w);
  // App-wide: a closed modal dialog resyncs the window it sat over, again once that is active.
  void installHoverResync();

}  // namespace stencil::support
