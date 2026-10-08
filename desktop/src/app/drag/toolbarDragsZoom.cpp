#include "toolbarDrags.hpp"
#include "zoomFollow.hpp"
#include "iconDrag.hpp"

#include <QAbstractButton>
#include <QPointer>
#include <QTimer>
#include <cmath>
#include <memory>

// The zoom icons' drags: − and + follow the pointer's distance, fit steps over whichever it rests
// on. Browser twin: browser/js/ui/drag/zoomDrag.js.

namespace stencil::gui {

  using support::IconDragHooks;
  using support::IconDragPoint;

  namespace {
    bool over(const QAbstractButton* icon, const QPoint& global) {
      return icon && icon->isVisible() && QRect(icon->mapToGlobal(QPoint(0, 0)), icon->size()).contains(global);
    }
  }  // namespace

  void installZoomDrag(QAbstractButton* icon, DragBegin begin, int sign, ZoomView view) {
    if (!icon || !view.zoom || !view.hold) return;
    struct State {
      double z0 = 1.0;
      QPointF centre;
      ZoomHold held;
    };
    auto s = std::make_shared<State>();
    const auto follow = [s, sign](const IconDragPoint& p) {
      const QPointF d = QPointF(p.global) - s->centre;
      s->held.at(zoomFromDistance(s->z0, std::hypot(d.x(), d.y()), sign));
    };
    IconDragHooks hooks;
    hooks.start = [icon = QPointer<QAbstractButton>(icon), begin, view, s](const QPoint&, const QPoint&) {
      if (!icon || (begin && !begin())) return false;
      s->z0 = view.zoom();   // a press steps nothing here: the click zooms on its release
      s->centre = icon->mapToGlobal(QRectF(icon->rect()).center());
      s->held = view.hold();
      return true;
    };
    hooks.move = follow;
    hooks.drop = follow;
    hooks.cancel = [s] { s->held.restore(); };
    support::installIconDrag(icon, std::move(hooks));
  }

  void installFitDrag(QAbstractButton* fit, QAbstractButton* out, QAbstractButton* in, DragBegin begin,
                      ZoomView view) {
    if (!fit || !out || !in || !view.zoom || !view.hold) return;
    struct State {
      QPointer<QAbstractButton> out, in;
      QPointer<QTimer> tick;
      int sign = 0;   // the way the zoom steps: -1 on −, +1 on +, 0 elsewhere
      ZoomHold held;
    };
    auto s = std::make_shared<State>();
    s->out = out;
    s->in = in;
    s->tick = new QTimer(fit);
    s->tick->setInterval(HOLD_ZOOM_TICK_MS);
    const auto step = [s, zoom = view.zoom] {
      if (s->sign) s->held.at(holdZoomStep(zoom(), s->sign));
    };
    QObject::connect(s->tick, &QTimer::timeout, fit, step);
    // Both shine as targets; the one under the pointer brighter.
    const auto shine = [s](bool on) {
      if (s->out) support::markDropTarget(s->out, on, s->sign < 0);
      if (s->in) support::markDropTarget(s->in, on, s->sign > 0);
    };
    const auto end = [s, shine] {
      if (s->tick) s->tick->stop();
      s->sign = 0;
      shine(false);
    };
    IconDragHooks hooks;
    hooks.start = [begin, view, s, shine](const QPoint&, const QPoint&) {
      if (begin && !begin()) return false;
      s->held = view.hold();
      s->sign = 0;
      shine(true);
      return true;
    };
    // Arriving on − or + steps at once, then every tick while the pointer rests there.
    hooks.move = [s, shine, step](const IconDragPoint& p) {
      const int sign = over(s->in, p.global) ? 1 : over(s->out, p.global) ? -1 : 0;
      if (sign == s->sign) return;
      s->sign = sign;
      shine(true);
      if (s->tick) s->tick->stop();
      if (!sign) return;
      step();
      if (s->tick) s->tick->start();
    };
    hooks.drop = [end](const IconDragPoint&) { end(); };
    hooks.cancel = [s, end] {
      end();
      s->held.restore();
    };
    support::installIconDrag(fit, std::move(hooks));
  }

}  // namespace stencil::gui
