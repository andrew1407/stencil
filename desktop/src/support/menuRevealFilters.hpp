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
        : QObject(menu), menu_(menu), origin_(std::move(origin)), ms_(ms) {
      menu->installEventFilter(this);
      // aboutToHide, NOT QEvent::Hide: the popup must still be on screen to photograph.
      QObject::connect(menu, &QMenu::aboutToHide, this, [this] {
        if (menu_) dustMenuOut(menu_, origin_(), ms_);
      });
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
      if (watched == menu_ && event->type() == QEvent::Show && !played_) {
        played_ = true;
        // Veiled on the Show itself: play() runs a tick later and a keyboard-opened
        // submenu would get a full-size frame in between.
        menu_->setWindowOpacity(0.0);
        // Deferred one tick: Show can fire inside QMenu::popup()/internalDelayedPopup(), and grabbing or
        // animating reentrantly there corrupts QMenu's sloppy-hover bookkeeping.
        QPointer<QMenu> guard(menu_);
        QTimer::singleShot(0, menu_, [this, guard] {
          if (guard && guard->isVisible()) play();
        });
      } else if (watched == menu_ && event->type() == QEvent::Hide) {
        settle();
        played_ = false;
        settled_ = false;
      }
      return QObject::eventFilter(watched, event);
    }

   private:
    void play() {
      QMenu* m = menu_;
      target_ = m->geometry();
      if (!target_.isValid()) { m->setWindowOpacity(1.0); return; }
      const QPoint origin = origin_();
      if (dustMenuIn(m, origin, ms_)) { settled_ = true; return; }
      // ~1/3 size, keeping the click point at the same fractional spot as in the final rect.
      const QPoint a(qBound(target_.left(), origin.x(), target_.right()),
                     qBound(target_.top(), origin.y(), target_.bottom()));
      const int w = qMax(target_.width() / 3, 24);
      const int h = qMax(target_.height() / 3, 24);
      const double fx = double(a.x() - target_.left()) / qMax(target_.width(), 1);
      const double fy = double(a.y() - target_.top()) / qMax(target_.height(), 1);
      const QRect start(QPoint(a.x() - int(fx * w), a.y() - int(fy * h)), QSize(w, h));
      // The chat menu is fixed-width and setGeometry clamps to constraints; settle() restores.
      savedMin_ = m->minimumSize();
      m->setMinimumSize(1, 1);
      m->setWindowOpacity(0.0);
      m->setGeometry(start);

      auto* geo = new QPropertyAnimation(m, "geometry", this);
      geo->setDuration(MENU_MS * ms_ / MENU_POPUP_DUST_MS);  // the fallback keeps the ratio
      geo->setStartValue(start);
      geo->setEndValue(target_);
      geo->setEasingCurve(QEasingCurve::OutCubic);
      auto* fade = new QPropertyAnimation(m, "windowOpacity", this);
      fade->setDuration(MENU_MS * ms_ / MENU_POPUP_DUST_MS);
      fade->setStartValue(0.0);
      fade->setEndValue(1.0);
      fade->setEasingCurve(QEasingCurve::OutCubic);
      group_ = new QParallelAnimationGroup(this);
      group_->addAnimation(geo);
      group_->addAnimation(fade);
      connect(group_, &QParallelAnimationGroup::finished, this, [this] { settle(); });
      group_->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // Idempotent.
    void settle() {
      if (group_) { group_->stop(); group_ = nullptr; }  // stop → DeleteWhenStopped
      if (!menu_ || !played_ || settled_) return;
      settled_ = true;
      menu_->setMinimumSize(savedMin_);
      menu_->setWindowOpacity(1.0);
      if (target_.isValid() && menu_->isVisible()) menu_->setGeometry(target_);
    }

    QPointer<QMenu> menu_;
    QPointer<QParallelAnimationGroup> group_;
    std::function<QPoint()> origin_;
    QRect target_;
    QSize savedMin_;
    int ms_ = MENU_POPUP_DUST_MS;
    bool played_ = false, settled_ = false;
  };

  // Backstop for Qt's own submenu-closing heuristic, off hovered() rather than native mouse moves (a
  // WA_TranslucentBackground popup loses those). Only POINTER hovers count, not a keyboard flyout's.
  class SubmenuCloseGuard : public QObject {
   public:
    SubmenuCloseGuard(QMenu* sub, QMenu* parent, QAction* parentAction)
        : QObject(sub), sub_(sub), parent_(parent), parentAction_(parentAction) {
      parent->installEventFilter(this);
      sub->installEventFilter(this);
      timer_.setSingleShot(true);
      connect(&timer_, &QTimer::timeout, this, [this] {
        if (!sub_ || !sub_->isVisible() || !parent_) return;
        // Check geometry, not Enter/Leave: a diagonal move may already have reached `sub`.
        if (sub_->geometry().contains(QCursor::pos())) return;
        if (sub_->geometry().contains(parent_->mapToGlobal(pointer_))) return;
        // MenuReveal's aboutToHide handler already flies this close; dusting here flies it twice.
        sub_->hide();
      });
      connect(parent, &QMenu::hovered, this, [this](QAction* a) {
        if (a == parentAction_) { timer_.stop(); lastHovered_ = a; return; }
        // hovered(QAction*) re-fires for the row already hovered; only a new row restarts the clock.
        if (a == lastHovered_) return;
        lastHovered_ = a;
        if (!movedSinceShow_ || !pointerOnRow(a)) return;
        timer_.start(graceMs());
      });
      // Never leave the timer armed against a `sub` (or parent) that is already gone.
      connect(parent, &QMenu::aboutToHide, this, [this] { timer_.stop(); });
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
      if (watched == parent_ && event->type() == QEvent::MouseMove) {
        // Before the parent's own handler emits hovered(), so the flag is current then.
        const QPoint p = static_cast<QMouseEvent*>(event)->position().toPoint();
        if (p != pointer_) { pointer_ = p; movedSinceShow_ = true; }
      } else if (watched == sub_ && event->type() == QEvent::Show) {
        movedSinceShow_ = false;
        timer_.stop();
      }
      return QObject::eventFilter(watched, event);
    }

   private:
    bool pointerOnRow(QAction* a) const {
      if (!parent_ || !a) return false;
      const QRect row = parent_->actionGeometry(a);
      return row.isValid() && row.contains(pointer_);
    }

    // A submenu flipped LEFT sits across the parent's whole width, so it needs a longer grace.
    int graceMs() const {
      if (!sub_ || !parent_ || !parentAction_) return 220;
      // actionGeometry() is parent-local; sub_->geometry() is global.
      const QRect row = parent_->actionGeometry(parentAction_);
      if (!row.isValid()) return 220;
      const int rowGlobalLeft = parent_->mapToGlobal(row.topLeft()).x();
      return sub_->geometry().left() < rowGlobalLeft ? 480 : 220;
    }

    QPointer<QMenu> sub_;
    QPointer<QMenu> parent_;
    QPointer<QAction> parentAction_;
    QPointer<QAction> lastHovered_;
    QTimer timer_;
    QPoint pointer_{-1, -1};      // last mouse-move position seen on the parent (local)
    bool movedSinceShow_ = false;  // …and whether it moved at all since `sub` came up
  };

  // A button's menu is built ONCE and popped many times, so this plays on every Show.
  class MenuFlight : public QObject {
   public:
    MenuFlight(QMenu* menu, QWidget* anchor) : QObject(menu), menu_(menu), anchor_(anchor) {
      // Named so a test can see the wiring; the flight itself is skipped offscreen.
      setObjectName(QStringLiteral("stencilMenuFlight"));
      menu->installEventFilter(this);
      // aboutToHide, NOT QEvent::Hide: the popup must still be on screen to photograph.
      QObject::connect(menu, &QMenu::aboutToHide, this, [this] {
        if (menu_ && anchor_) dismissPopup(*menu_, anchor_, MENU_POPUP_DUST_MS);
      });
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
      if (watched == menu_ && event->type() == QEvent::Show && menu_ && anchor_)
        revealPopup(*menu_, anchor_, MENU_POPUP_DUST_MS);
      return QObject::eventFilter(watched, event);
    }

   private:
    QPointer<QMenu> menu_;
    QPointer<QWidget> anchor_;
  };
}  // namespace stencil::support
