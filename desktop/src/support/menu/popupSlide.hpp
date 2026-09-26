#pragma once
// The 'slide' motion mode's entrance for a popup list (a selector's, a control-hung menu's): it
// grows out of its origin — the combo's caret, where the dust would fly from. Browser twin:
// css/animations/overlays.css menuFromAnchor, its origin set as ui/control/dropdownMenu.js does.
#include <QPoint>
#include <QRect>
#include <QtGlobal>
#include <cmath>

class QWidget;

namespace stencil::support {

  inline constexpr int POPUP_SLIDE_MS = 220;
  inline constexpr double POPUP_SLIDE_SCALE = 0.66;
  inline constexpr int POPUP_SLIDE_LIFT = 6;         // px the start frame sits above its end
  inline constexpr double POPUP_SLIDE_FADE_AT = 0.22;   // share of the run by which it is opaque

  // The first frame: `target` scaled to 0.66 around the origin clamped into it — its top edge for
  // a list below the combo, its bottom edge for one above — kept at the same fractional spot.
  inline QRect slideStartRect(const QRect& target, const QPoint& origin) {
    if (!target.isValid()) return target;
    const QPoint a(qBound(target.left(), origin.x(), target.right()),
                   qBound(target.top(), origin.y(), target.bottom()));
    const int w = qMax(1, int(std::lround(target.width() * POPUP_SLIDE_SCALE)));
    const int h = qMax(1, int(std::lround(target.height() * POPUP_SLIDE_SCALE)));
    const double fx = double(a.x() - target.left()) / qMax(target.width() - 1, 1);
    const double fy = double(a.y() - target.top()) / qMax(target.height() - 1, 1);
    const QPoint topLeft(a.x() - int(std::lround(fx * (w - 1))),
                         a.y() - int(std::lround(fy * (h - 1))) - POPUP_SLIDE_LIFT);
    return QRect(topLeft, QSize(w, h));
  }

  // Veils `popup` now and grows it from `originGlobal` a tick later, to the geometry it has then.
  void slidePopupIn(QWidget& popup, const QPoint& originGlobal);

}  // namespace stencil::support
