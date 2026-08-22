#pragma once
#include <QRect>
#include <QSize>

// ── Modal popovers: placement for the compact, icon-anchored dialog shape ────
// Double-click / right-click on a dialog-opening toolbar icon opens the SAME dialog as a
// small frameless popover pinned next to the icon instead of centred over the window
// (mainWindow.cpp execMaybePopover). This is the desktop port of the browser's
// popoverPosition (browser/js/ui/popover.js) — keep the two rule-for-rule: below the
// anchor with left edges aligned, flipped above when the bottom would overflow, clamped
// inside the screen on both axes. Header-only and pure so the headless test drives it
// without a window.
namespace stencil::support {

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
