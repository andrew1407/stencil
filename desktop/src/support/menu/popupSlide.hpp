#pragma once
// The 'slide' motion mode's entrance for a popup list (a selector's, a control-hung menu's): its picture
// pours out of its origin — the combo's caret — as a window's does (support/genie/), the list itself
// shown as it lands; without a picture it grows from the origin instead. Browser twin:
// ui/control/dropdownMenu.js over motion/genie/.
#include <QPoint>
#include <QRect>
#include <QEasingCurve>
#include <QtGlobal>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <utility>

class QLabel;
class QWidget;

namespace stencil::support {

  inline constexpr int POPUP_SLIDE_MS = 260;   // browser menuFromAnchor 0.26s
  inline constexpr int POPUP_GENIE_MS = 420;   // a list is opened to be clicked: quicker than a window, still a pour
  inline constexpr int POPUP_GENIE_ORIGIN_PX = 12;   // the box at the selector's arrow a list pours out of
  inline constexpr double POPUP_SLIDE_SCALE = 0.05;   // a speck at the origin: Transition grows it all the way
  inline constexpr int POPUP_SLIDE_LIFT = 6;         // px the start frame sits above its end
  inline constexpr double POPUP_SLIDE_FADE_AT = 0.08;   // share of the run by which it is opaque: seen small

  // The first frame: `target` scaled to POPUP_SLIDE_SCALE around the origin clamped into it — its top edge for
  // a list below the combo, its bottom edge for one above — kept at the same fractional spot.
  inline QRect slideStartRect(const QRect& target, const QPoint& origin, double scale = POPUP_SLIDE_SCALE,
                              int lift = POPUP_SLIDE_LIFT) {
    if (!target.isValid()) return target;
    const QPoint a(qBound(target.left(), origin.x(), target.right()),
                   qBound(target.top(), origin.y(), target.bottom()));
    const int w = qMax(1, int(std::lround(target.width() * scale)));
    const int h = qMax(1, int(std::lround(target.height() * scale)));
    const double fx = double(a.x() - target.left()) / qMax(target.width() - 1, 1);
    const double fy = double(a.y() - target.top()) / qMax(target.height() - 1, 1);
    const QPoint topLeft(a.x() - int(std::lround(fx * (w - 1))),
                         a.y() - int(std::lround(fy * (h - 1))) - lift);
    return QRect(topLeft, QSize(w, h));
  }

  // The strip of `anchor` that faces `box` (its underside for a list below it, its top for one above),
  // which a list pours out of and back into; `anchor` itself when the two overlap. GLOBAL rects.
  inline QRect pourEdge(const QRect& anchor, const QRect& box) {
    if (!anchor.isValid()) return anchor;
    if (box.top() >= anchor.bottom() - 1) return QRect(anchor.left(), anchor.bottom() - 1, anchor.width(), 2);
    if (box.bottom() <= anchor.top() + 1) return QRect(anchor.left(), anchor.top(), anchor.width(), 2);
    return anchor;
  }

  // The box at the selector's arrow (`originGlobal`, the caret) a list pours out of and back into.
  inline QRect arrowBox(const QPoint& originGlobal) {
    QRect box(QPoint(), QSize(POPUP_GENIE_ORIGIN_PX, POPUP_GENIE_ORIGIN_PX));
    box.moveCenter(originGlobal);
    return box;
  }

  // Veils `popup` now and grows it from `originGlobal` a tick later, to the geometry it has then;
  // Sliding pours it out of the arrow there instead.
  void slidePopupIn(QWidget& popup, const QPoint& originGlobal, int ms = POPUP_GENIE_MS);
  // Transition's exit for a list or a menu closing: its picture shrinks back toward `originGlobal` and fades
  // (browser menuToAnchor, 0.18s), in a click-through window of its own; false with nothing to photograph.
  inline constexpr int POPUP_SHRINK_MS = 180;
  bool shrinkPopupOut(QWidget& popup, const QPoint& originGlobal);
  // Sliding's exit: the hidden list's picture poured back into the arrow at `originGlobal`, its ghost
  // a window of `anchor`'s. False when it cannot fly (no picture, no anchor on screen).
  bool slidePopupOut(QWidget& popup, QWidget* anchor, const QPoint& originGlobal, int ms = POPUP_GENIE_MS);
  // `popup`'s picture in a click-through window of its own, its ghost (dropPopupGhosts hides it); null with nothing to photograph.
  // playPopupGhost flies it `from` → `to` in `ms`, its opacity on the `alpha` (progress, opacity) keys, then closes it.
  QLabel* popupPictureGhost(QWidget& popup, const QRect& at);
  void playPopupGhost(QLabel* ghost, const QRect& from, const QRect& to, int ms, const QEasingCurve& curve,
                      std::initializer_list<std::pair<double, double>> alpha, std::function<void()> done = {});
  // Hides what is left of `popup`'s last leave (its pour or shrink): a quick re-show never stands on its old picture.
  void dropPopupGhosts(QWidget& popup);
  // A menu's Sliding pour out of `originGlobal` (a menu-bar title, the pointer, a row's caret) and back
  // into it; false when Sliding is off or there is nothing to photograph. In, the menu is veiled until it lands.
  bool pourMenuIn(QWidget* menu, const QPoint& originGlobal);
  bool pourMenuOut(QWidget* menu, const QPoint& originGlobal);

}  // namespace stencil::support
