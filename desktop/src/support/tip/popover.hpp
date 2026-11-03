#pragma once
#include <QRect>
#include <QSize>

// Popover placement for a dialog opened next to its toolbar icon (MainWindow.cpp
// execMaybePopover) — the desktop port of browser/js/ui/tip/popover.js popoverPosition; keep
// the two rule-for-rule. Header-only and pure, so the headless test drives it.
namespace stencil::support {

  // The part of `window` its screen shows (`avail`, the screen less dock and menu bar): a popover kept to the window
  // alone opened off-screen when the window hangs over the edge. The whole window when they do not meet.
  inline QRect visiblePart(const QRect& window, const QRect& avail) {
    const QRect seen = window.intersected(avail);
    return seen.isEmpty() ? window : seen;
  }

  inline QRect popoverRect(const QRect& anchor, const QSize& box, const QRect& screen,
                           int gap = 8, int margin = 8) {
    int top = anchor.bottom() + 1 + gap;   // QRect::bottom() is the last inside pixel
    if (top + box.height() > screen.bottom() + 1 - margin) {
      const int above = anchor.top() - gap - box.height();
      top = above >= screen.top() + margin
                ? above
                : qMax(screen.top() + margin, screen.bottom() + 1 - margin - box.height());
    }
    const int left = qMax(screen.left() + margin,
                          qMin(anchor.left(), screen.right() + 1 - margin - box.width()));
    return QRect(QPoint(left, top), box);
  }

}  // namespace stencil::support
