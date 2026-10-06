#include "menuCheckSwap.hpp"
#include "controlSwap.hpp"

#include <QAction>
#include <QMenu>

#include <cmath>

namespace stencil::gui {

  namespace {
    // Device-pixel bounds of what differs between two same-sized grabs; empty when nothing does.
    QRect changedBounds(const QImage& a, const QImage& b) {
      if (a.size() != b.size()) return {};
      int x0 = a.width(), y0 = a.height(), x1 = -1, y1 = -1;
      for (int y = 0; y < a.height(); ++y) {
        const auto* ra = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const auto* rb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
          if (ra[x] == rb[x]) continue;
          x0 = std::min(x0, x); y0 = std::min(y0, y);
          x1 = std::max(x1, x); y1 = std::max(y1, y);
        }
      }
      return x1 < 0 ? QRect() : QRect(QPoint(x0, y0), QPoint(x1, y1));
    }

    QImage argb(const QPixmap& p) { return p.toImage().convertToFormat(QImage::Format_ARGB32); }

    struct RowShot {
      QAction* action;
      QRect row;
      QPixmap before;
      bool was;
    };
  }  // namespace

  void toggleMenuRowsWithDust(QMenu* menu, const QList<QAction*>& rows,
                              const std::function<void()>& apply) {
    std::vector<RowShot> shots;
    if (menu && menu->isVisible() && !support::motionReduced()) {
      for (QAction* a : rows) {
        const QRect row = a ? menu->actionGeometry(a) : QRect();
        if (row.isValid() && !row.isEmpty()) shots.push_back({a, row, menu->grab(row), a->isChecked()});
      }
    }
    if (apply) apply();
    for (const RowShot& s : shots) {
      const bool on = s.action->isChecked();
      if (on == s.was) continue;
      const QPixmap after = menu->grab(s.row);
      const QRect dev = changedBounds(argb(s.before), argb(after));
      if (dev.isEmpty()) continue;
      const qreal dpr = after.devicePixelRatio() > 0 ? after.devicePixelRatio() : 1.0;
      const QRect at(s.row.topLeft() + QPoint(int(std::floor(dev.x() / dpr)), int(std::floor(dev.y() / dpr))),
                     QSize(int(std::ceil(dev.width() / dpr)), int(std::ceil(dev.height() / dpr))));
      const QPixmap now = after.copy(dev), then = s.before.copy(dev);
      DisintegrateOverlay* fx = DisintegrateOverlay::overPixmaps(
          on ? now : then, on ? then : now, at, menu,
          on ? DisintegrateOverlay::Sweep::GATHER : DisintegrateOverlay::Sweep::FALL,
          CHECK_SWAP_CELLS, CHECK_SWAP_CELLS, CHECK_SWAP_MS, CHECK_SWAP_SPREAD, CHECK_SWAP_PAD_PX,
          QString::fromLatin1(CHECK_SWAP_OBJECT_NAME));
      if (fx) fx->bindToSurface(menu);
    }
  }

}  // namespace stencil::gui
