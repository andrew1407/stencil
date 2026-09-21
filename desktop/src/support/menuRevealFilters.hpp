#pragma once
// The three event filters behind a revealed menu: the reveal itself (grab, dust, fade), the guard
// that keeps a submenu from closing under the pointer, and the flight a menu takes from its anchor.
// Private to the menuReveal TUs.
#include "menuRevealDust.hpp"

#include <QAbstractAnimation>
#include <QAction>
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QGuiApplication>
#include <QMenuBar>
#include <QMouseEvent>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRect>
#include <QTimer>
#include <functional>

namespace stencil::support {

  // Re-arms on Hide: a submenu is the SAME QMenu instance shown many times per session.
  // `origin` is a resolver because a submenu's origin is only known once Show fires.
  class MenuReveal : public QObject {
   public:
    MenuReveal(QMenu* menu, std::function<QPoint()> origin, int ms)
        : QObject(menu), menu(menu), origin(std::move(origin)), ms(ms) {
      menu->installEventFilter(this);
      // aboutToHide, NOT QEvent::Hide: the popup must still be on screen to photograph.
      QObject::connect(menu, &QMenu::aboutToHide, this, [this] {
        if (this->menu) dustMenuOut(this->menu, this->origin(), this->ms);
      });
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
      if (watched == menu && event->type() == QEvent::Show && !played) {
        played = true;
        // Veiled on the Show itself: play() runs a tick later and a keyboard-opened
        // submenu would get a full-size frame in between.
        menu->setWindowOpacity(0.0);
        // Deferred one tick: Show can fire inside QMenu::popup()/internalDelayedPopup(), and grabbing or
        // animating reentrantly there corrupts QMenu's sloppy-hover bookkeeping.
        QPointer<QMenu> guard(menu);
        QTimer::singleShot(0, menu, [this, guard] {
          if (guard && guard->isVisible()) play();
        });
      } else if (watched == menu && event->type() == QEvent::Hide) {
        settle();
        played = false;
        settled = false;
      }
      return QObject::eventFilter(watched, event);
    }

   private:
    void play() {
      QMenu* m = menu;
      target = m->geometry();
      if (!target.isValid()) { m->setWindowOpacity(1.0); return; }
      const QPoint origin = this->origin();
      if (dustMenuIn(m, origin, ms)) { settled = true; return; }
      // ~1/3 size, keeping the click point at the same fractional spot as in the final rect.
      const QPoint a(qBound(target.left(), origin.x(), target.right()),
                     qBound(target.top(), origin.y(), target.bottom()));
      const int w = qMax(target.width() / 3, 24);
      const int h = qMax(target.height() / 3, 24);
      const double fx = double(a.x() - target.left()) / qMax(target.width(), 1);
      const double fy = double(a.y() - target.top()) / qMax(target.height(), 1);
      const QRect start(QPoint(a.x() - int(fx * w), a.y() - int(fy * h)), QSize(w, h));
      // The chat menu is fixed-width and setGeometry clamps to constraints; settle() restores.
      savedMin = m->minimumSize();
      m->setMinimumSize(1, 1);
      m->setWindowOpacity(0.0);
      m->setGeometry(start);

      auto* geo = new QPropertyAnimation(m, "geometry", this);
      geo->setDuration(MENU_MS * ms / MENU_POPUP_DUST_MS);  // the fallback keeps the ratio
      geo->setStartValue(start);
      geo->setEndValue(target);
      geo->setEasingCurve(QEasingCurve::OutCubic);
      auto* fade = new QPropertyAnimation(m, "windowOpacity", this);
      fade->setDuration(MENU_MS * ms / MENU_POPUP_DUST_MS);
      fade->setStartValue(0.0);
      fade->setEndValue(1.0);
      fade->setEasingCurve(QEasingCurve::OutCubic);
      group = new QParallelAnimationGroup(this);
      group->addAnimation(geo);
      group->addAnimation(fade);
      connect(group, &QParallelAnimationGroup::finished, this, [this] { settle(); });
      group->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // Idempotent.
    void settle() {
      if (group) { group->stop(); group = nullptr; }  // stop → DeleteWhenStopped
      if (!menu || !played || settled) return;
      settled = true;
      menu->setMinimumSize(savedMin);
      menu->setWindowOpacity(1.0);
      if (target.isValid() && menu->isVisible()) menu->setGeometry(target);
    }

    QPointer<QMenu> menu;
    QPointer<QParallelAnimationGroup> group;
    std::function<QPoint()> origin;
    QRect target;
    QSize savedMin;
    int ms = MENU_POPUP_DUST_MS;
    bool played = false, settled = false;
  };

  // Backstop for Qt's own submenu-closing heuristic, off hovered() rather than native mouse moves (a
  // WA_TranslucentBackground popup loses those). Only POINTER hovers count, not a keyboard flyout's.
  class SubmenuCloseGuard : public QObject {
   public:
    SubmenuCloseGuard(QMenu* sub, QMenu* parent, QAction* parentAction)
        : QObject(sub), sub(sub), parent(parent), parentAction(parentAction) {
      parent->installEventFilter(this);
      sub->installEventFilter(this);
      timer.setSingleShot(true);
      connect(&timer, &QTimer::timeout, this, [this] {
        if (!this->sub || !this->sub->isVisible() || !this->parent) return;
        // Check geometry, not Enter/Leave: a diagonal move may already have reached `sub`.
        if (this->sub->geometry().contains(QCursor::pos())) return;
        if (this->sub->geometry().contains(this->parent->mapToGlobal(pointer))) return;
        // MenuReveal's aboutToHide handler already flies this close; dusting here flies it twice.
        this->sub->hide();
      });
      connect(parent, &QMenu::hovered, this, [this](QAction* a) {
        if (a == this->parentAction) { timer.stop(); lastHovered = a; return; }
        // hovered(QAction*) re-fires for the row already hovered; only a new row restarts the clock.
        if (a == lastHovered) return;
        lastHovered = a;
        if (!movedSinceShow || !pointerOnRow(a)) return;
        timer.start(graceMs());
      });
      // Never leave the timer armed against a `sub` (or parent) that is already gone.
      connect(parent, &QMenu::aboutToHide, this, [this] { timer.stop(); });
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
      if (watched == parent && event->type() == QEvent::MouseMove) {
        // Before the parent's own handler emits hovered(), so the flag is current then.
        const QPoint p = static_cast<QMouseEvent*>(event)->position().toPoint();
        if (p != pointer) { pointer = p; movedSinceShow = true; }
      } else if (watched == sub && event->type() == QEvent::Show) {
        movedSinceShow = false;
        timer.stop();
      }
      return QObject::eventFilter(watched, event);
    }

   private:
    bool pointerOnRow(QAction* a) const {
      if (!parent || !a) return false;
      const QRect row = parent->actionGeometry(a);
      return row.isValid() && row.contains(pointer);
    }

    // A submenu flipped LEFT sits across the parent's whole width, so it needs a longer grace.
    int graceMs() const {
      if (!sub || !parent || !parentAction) return 220;
      // actionGeometry() is parent-local; sub->geometry() is global.
      const QRect row = parent->actionGeometry(parentAction);
      if (!row.isValid()) return 220;
      const int rowGlobalLeft = parent->mapToGlobal(row.topLeft()).x();
      return sub->geometry().left() < rowGlobalLeft ? 480 : 220;
    }

    QPointer<QMenu> sub;
    QPointer<QMenu> parent;
    QPointer<QAction> parentAction;
    QPointer<QAction> lastHovered;
    QTimer timer;
    QPoint pointer{-1, -1};      // last mouse-move position seen on the parent (local)
    bool movedSinceShow = false;  // …and whether it moved at all since `sub` came up
  };

  // A button's menu is built ONCE and popped many times, so this plays on every Show.
  class MenuFlight : public QObject {
   public:
    MenuFlight(QMenu* menu, QWidget* anchor) : QObject(menu), menu(menu), anchor(anchor) {
      // Named so a test can see the wiring; the flight itself is skipped offscreen.
      setObjectName(QStringLiteral("stencilMenuFlight"));
      menu->installEventFilter(this);
      // aboutToHide, NOT QEvent::Hide: the popup must still be on screen to photograph.
      QObject::connect(menu, &QMenu::aboutToHide, this, [this] {
        if (this->menu && this->anchor) dismissPopup(*this->menu, this->anchor, MENU_POPUP_DUST_MS);
      });
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
      if (watched == menu && event->type() == QEvent::Show && menu && anchor)
        revealPopup(*menu, anchor, MENU_POPUP_DUST_MS);
      return QObject::eventFilter(watched, event);
    }

   private:
    QPointer<QMenu> menu;
    QPointer<QWidget> anchor;
  };
}  // namespace stencil::support
