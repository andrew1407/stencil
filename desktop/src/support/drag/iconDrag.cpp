#include "iconDrag.hpp"
#include "dragOverlays.hpp"
#include "AppTooltip.hpp"
#include "../uiTimings.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>
#include <QToolTip>
#include <cmath>

namespace stencil::support {

  IconDragMachine::IconDragMachine(IconDragHooks hooks, std::function<QRect()> originRect,
                                   std::function<QWidget*(const QPoint&)> targetAt)
      : hooks(std::move(hooks)), originRect(std::move(originRect)), targetAt(std::move(targetAt)) {}

  IconDragPoint IconDragMachine::at(const QPoint& global) const {
    return {global, originRect && originRect().contains(global), targetAt ? targetAt(global) : nullptr};
  }

  void IconDragMachine::press(const QPoint& global) {
    pressAt = global;
    pressed = true;
    dragging = false;
  }

  bool IconDragMachine::move(const QPoint& global) {
    if (!pressed) return false;
    if (!dragging) {
      const QPoint d = global - pressAt;
      if (std::hypot(d.x(), d.y()) <= uiTimings().pressSlopPx) return false;
      if (hooks.start && !hooks.start(pressAt, global)) {
        pressed = false;
        return false;
      }
      dragging = true;
      ++dragsStarted();
    }
    if (hooks.move) hooks.move(at(global));
    return true;
  }

  bool IconDragMachine::release(const QPoint& global) {
    const bool was = dragging;
    pressed = dragging = false;
    if (!was) return false;
    const IconDragPoint p = at(global);
    if (p.overOrigin) {
      if (hooks.cancel) hooks.cancel();
    } else if (hooks.drop) {
      hooks.drop(p);
    }
    return true;
  }

  bool IconDragMachine::abort() {
    const bool was = dragging;
    pressed = dragging = false;
    if (was && hooks.cancel) hooks.cancel();
    return was;
  }

  namespace {

    const char* const FILTER_NAME = "stencilIconDrag";

    // While a drag lives no tip shows: Qt's, the app's (gone at once, its dust with it, and held
    // off until the drag ends) and any window marked as a tip.
    void holdTips(bool on) {
      if (on) QToolTip::hideText();
      for (QWidget* top : QApplication::topLevelWidgets()) {
        if (top->objectName() == QLatin1String(gui::AppTooltip::OBJECT_NAME))
          static_cast<gui::AppTooltip*>(top)->holdOff(on);
        else if (on && top->isVisible() && top->property(gui::TIP_WINDOW_PROPERTY).toBool())
          top->hide();
      }
    }

    // The source's press runs untouched until the drag starts; from then on it sees no moves, and a
    // button sees the pointer leave (its release clicks nothing) where any other loses the release.
    class IconDragFilter : public QObject {
     public:
      IconDragFilter(QWidget* source, IconDragHooks hooks)
          : QObject(source),
            source(source),
            machine(deferred(std::move(hooks)), [this] { return origin(); },
                    [](const QPoint& g) { return QApplication::widgetAt(g); }) {
        setObjectName(QLatin1String(FILTER_NAME));
        ghostWanted = machine.hooks.ghost;
      }

      bool active() const { return machine.active(); }

     protected:
      bool eventFilter(QObject* obj, QEvent* ev) override {
        if (grabbed && ev->type() == QEvent::ToolTip) return true;   // no tip shows while a drag is live
        if (obj != source) return appEvent(ev);
        switch (ev->type()) {
          case QEvent::MouseButtonPress:
          case QEvent::MouseButtonDblClick: {   // a double-click's second press drags as a first does
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() != Qt::LeftButton || !source->isEnabled()) return false;
            held = machine.hooks.grab ? machine.hooks.grab(me->globalPosition().toPoint()) : QRect();
            if (!machine.hooks.grab || !held.isEmpty()) machine.press(me->globalPosition().toPoint());
            return false;
          }
          case QEvent::MouseMove: {
            if (leaving) return false;
            if (grabbed && !machine.active()) return true;   // an Escaped drag: the press stays spent
            auto* me = static_cast<QMouseEvent*>(ev);
            const bool was = machine.active();
            if (!(me->buttons() & Qt::LeftButton) || !machine.move(me->globalPosition().toPoint())) return false;
            if (!was) begin(me->globalPosition().toPoint());
            ghostFollow(me->globalPosition().toPoint());
            return true;
          }
          case QEvent::MouseButtonRelease: {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() != Qt::LeftButton) return false;
            const bool spent = grabbed && !button();
            grabbed = false;
            end();
            machine.release(me->globalPosition().toPoint());
            return spent;
          }
          default:
            return false;
        }
      }

     private:
      // The drop and the cancel wait for the next turn: the button has its release first.
      IconDragHooks deferred(IconDragHooks h) {
        auto later = [this](std::function<void()> fn) {
          QTimer::singleShot(0, source.data(), std::move(fn));
        };
        if (h.drop) h.drop = [later, fn = h.drop](const IconDragPoint& p) { later([fn, p] { fn(p); }); };
        if (h.cancel) h.cancel = [later, fn = h.cancel] { later(fn); };
        // Tips go before the start hook runs, so nothing it photographs carries one.
        h.start = [fn = h.start](const QPoint& from, const QPoint& global) {
          holdTips(true);
          const bool ok = !fn || fn(from, global);
          if (!ok) holdTips(false);
          return ok;
        };
        return h;
      }

      QAbstractButton* button() const { return qobject_cast<QAbstractButton*>(source.data()); }
      QRect origin() const {
        return held.isEmpty() ? QRect(source->mapToGlobal(QPoint(0, 0)), source->size()) : held;
      }

      void begin(const QPoint& global) {
        grabbed = true;
        leaving = true;
        QMouseEvent away(QEvent::MouseMove, QPointF(-1e5, -1e5), QPointF(-1e5, -1e5), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        if (button()) QApplication::sendEvent(button(), &away);
        leaving = false;
        if (ghostWanted)
          ghost = new DragGhost(source, machine.hooks.ghostCentred ? source->rect().center()
                                                                   : source->mapFromGlobal(global),
                                machine.hooks.face ? machine.hooks.face() : QPixmap());
        qApp->installEventFilter(this);
      }

      void ghostFollow(const QPoint& global) {
        if (ghost) ghost->follow(global);
      }

      void end() {
        qApp->removeEventFilter(this);
        delete ghost.data();
        if (grabbed || machine.active()) holdTips(false);
      }

      bool appEvent(QEvent* ev) {
        if (!machine.active()) return false;
        const bool escape = (ev->type() == QEvent::KeyPress || ev->type() == QEvent::ShortcutOverride) &&
                            static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape;
        if (!escape && ev->type() != QEvent::ApplicationDeactivate) return false;
        end();
        machine.abort();
        if (escape) ev->accept();
        return escape;
      }

      QPointer<QWidget> source;
      QRect held;   // what the press took hold of (IconDragHooks::grab); empty: the whole source
      IconDragMachine machine;
      QPointer<DragGhost> ghost;
      bool ghostWanted = true;
      bool grabbed = false;   // from the drag's start to the release, Escape or not
      bool leaving = false;
    };

    IconDragFilter* filterOf(const QWidget* source) {
      return source ? dynamic_cast<IconDragFilter*>(
                          source->findChild<QObject*>(QLatin1String(FILTER_NAME), Qt::FindDirectChildrenOnly))
                    : nullptr;
    }

  }  // namespace

  void installIconDrag(QWidget* source, IconDragHooks hooks) {
    if (!source || filterOf(source)) return;
    source->installEventFilter(new IconDragFilter(source, std::move(hooks)));
  }

  bool iconDragActive(const QWidget* source) {
    const IconDragFilter* f = filterOf(source);
    return f && f->active();
  }

}  // namespace stencil::support
