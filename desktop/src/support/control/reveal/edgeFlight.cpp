#include "edgeFlight.hpp"

#include "modeGhost.hpp"
#include "../../genie/GenieGhost.hpp"
#include "../../menu/popupSlide.hpp"   // POPUP_GENIE_MS
#include "../../motionPrefs.hpp"

#include <algorithm>
#include <utility>

namespace stencil::gui {

  namespace {
    // The 2x16 strip on the viewport's left edge, level with the row's middle (browser leftHome), global.
    QRect leftHome(QWidget* view, const QRect& r) {
      const QPoint o = view->mapToGlobal(QPoint(0, 0));
      return QRect(o.x() + 1, o.y() + std::max(0, r.center().y() - 8), 2, 16);
    }

    bool fly(QWidget* view, const QPixmap& shot, const QRect& r, bool in, std::function<void()> done) {
      if (!view || shot.isNull() || !r.isValid()) return false;
      if (support::isSlideMotionOk())
        return support::genie::fly(view, shot, QRect(view->mapToGlobal(r.topLeft()), r.size()), leftHome(view, r),
                                   in, /*asWindow=*/false, std::move(done), support::POPUP_GENIE_MS);
      return playModeGhost(view, shot, r, view->rect(), in, Qt::Horizontal, MODE_ROW_MS, std::move(done),
                           /*pill=*/false, /*slideAlways=*/true);
    }
  }  // namespace

  int edgeFlightMs() { return support::isSlideMotionOk() ? support::POPUP_GENIE_MS : MODE_ROW_MS; }

  bool edgeAway(QWidget* view, const QPixmap& shot, const QRect& r) { return fly(view, shot, r, false, {}); }

  bool edgeIn(QWidget* view, const QPixmap& shot, const QRect& r, std::function<void()> landed) {
    return fly(view, shot, r, true, std::move(landed));
  }

}  // namespace stencil::gui
