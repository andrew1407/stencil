#pragma once
// A toolbar popover's own entrance and exit (MainWindow::execMaybePopover): Sliding pours its picture
// out of the icon and back into it (support/genie/), as a window's; else it grows from and shrinks
// into the icon, fading. Rects are in `host`'s coordinates; `ms` is the tuned length.
#include <QPixmap>
#include <QRect>
#include <functional>

class QGraphicsOpacityEffect;
class QWidget;

namespace stencil::gui {

  // `fx` veils the overlay until it lands.
  void popoverEntrance(QWidget* host, QWidget* overlay, QGraphicsOpacityEffect* fx, const QPixmap& shot,
                       const QRect& fromBox, const QRect& box, int ms);
  // `end` runs once the overlay has gone home.
  void popoverExit(QWidget* host, QWidget* overlay, const QPixmap& shot, const QRect& fromBox, int ms,
                   std::function<void()> end);

}  // namespace stencil::gui
