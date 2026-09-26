#pragma once
// Hover after a modal loop: Qt keeps the hover it recorded before the dialog or popover ran, and
// macOS keeps the closed window's cursor on screen, so a still pointer wore a stale hand and the
// first widget it reached never heard an Enter. Browser twin: none — the DOM re-hit-tests itself.
class QWidget;

namespace stencil::support {

  // Enters the widget under the pointer when Qt has it un-hovered, and puts its cursor back on.
  void resyncHover(QWidget* window);
  // App-wide: a closed modal dialog resyncs the window it sat over, again once that is active.
  void installHoverResync();

}  // namespace stencil::support
