#include "menuReveal.hpp"
#include "disintegrateOverlay.hpp"
#include "modalReveal.hpp"  // motionReduced()

#include <algorithm>

#include <QAbstractAnimation>
#include <QAction>
#include <QComboBox>
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QGuiApplication>
#include <QPixmap>
#include <QMenu>
#include <QMenuBar>
#include <QParallelAnimationGroup>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRect>
#include <QTimer>
#include <QWidget>
#include <functional>

namespace stencil::support {

  namespace {
    // Snappier than the dialog flight — a menu should feel instant, not staged.
    constexpr int kMenuMs = 140;

    // The popup forms out of motes streaming from the point it was opened at — the same
    // flight every other surface plays (browser js/ui/motion.js surfaceIn). Drawn inside
    // the window the menu belongs to, since the overlay is a child widget; a menu with no
    // such window (or one that declines) falls back to the grow-from-the-cursor pop.
    // escapeHost: a submenu can open past the host's own border; safe because
    // placeForSurface's escape layer is Qt::ToolTip, not a grab-stealing Qt::Window.
    bool dustPopupIn(QWidget* popup, QWidget* host, const QPoint& originGlobal, int ms) {
      if (!gui::flyTipDust(popup, host, originGlobal, /*gather=*/true, ms,
                           /*escapeHost=*/true))
        return false;
      // The popup waits behind its own dust and fades up as the last motes land.
      gui::fadeUpBehindDust(popup, ms);
      return true;
    }

    // …and the way back: the same box, pouring INTO `originGlobal`.
    bool dustPopupOut(QWidget* popup, QWidget* host, const QPoint& originGlobal, int ms) {
      return gui::flyTipDust(popup, host, originGlobal, /*gather=*/false, ms,
                             /*escapeHost=*/true)
             != nullptr;
    }

    // The real top-level app window a menu chain hangs off: a submenu's parentWidget() is
    // another QMenu, itself a popup (top-level), so plain ->window() stops there instead
    // of reaching the app window. Walk past every QMenu ancestor first.
    QWidget* menuHostWindow(QWidget* w) {
      while (w && qobject_cast<QMenu*>(w)) w = w->parentWidget();
      return w ? w->window() : nullptr;
    }

    bool dustMenuIn(QMenu* m, const QPoint& originGlobal) {
      return dustPopupIn(m, menuHostWindow(m->parentWidget()), originGlobal, kMenuPopupDustMs);
    }

    // Point-based sibling of the public dismissPopup(), which is anchor-widget-based
    // (MenuFlight's controls). Gated on the menu still being on screen — this fires
    // mid-hover (SubmenuCloseGuard), before Qt hides it.
    bool dustMenuOut(QMenu* m, const QPoint& originGlobal) {
      if (!m->isVisible()) return false;
      return dustPopupOut(m, menuHostWindow(m->parentWidget()), originGlobal, kMenuPopupDustMs);
    }

    // Plays the growth on every Show, settles on finish or an early Hide, and re-arms
    // on Hide so the NEXT Show plays again. `origin` is a resolver, not a fixed point —
    // a submenu's true origin (its parent row) is only known once Show fires and the
    // parent is laid out. A submenu built once and hover-opened/closed repeatedly within
    // one right-click session is the SAME QMenu instance shown many times — re-arming
    // is what makes the growth+dust replay every time instead of just the very first
    // (user report: no animation "after the first opening").
    class MenuReveal : public QObject {
     public:
      MenuReveal(QMenu* menu, std::function<QPoint()> origin)
          : QObject(menu), menu_(menu), origin_(std::move(origin)) {
        menu->installEventFilter(this);
        // aboutToHide, NOT QEvent::Hide: the popup must still be on screen to be
        // photographed (MenuFlight's own close does the same). Covers both the
        // top-level menu and every submenu, since both go through MenuReveal.
        QObject::connect(menu, &QMenu::aboutToHide, this, [this] {
          if (menu_) dustMenuOut(menu_, origin_());
        });
      }

     protected:
      bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == menu_ && event->type() == QEvent::Show && !played_) {
          played_ = true;
          // Deferred one tick: a submenu's Show can fire from deep inside Qt's own
          // QMenu::popup()/internalDelayedPopup() (the hover-delay submenu open).
          // Grabbing a pixmap, spawning the dust overlay's own top-level window and
          // animating geometry reentrantly on THAT call stack corrupts QMenu's popup/
          // sloppy-hover bookkeeping (crashed inside QMenuSloppyState::setSubMenuPopup).
          // Posting it instead runs once that call has fully unwound to the event loop.
          QPointer<QMenu> guard(menu_);
          QTimer::singleShot(0, menu_, [this, guard] {
            if (guard && guard->isVisible()) play();
          });
        } else if (watched == menu_ && event->type() == QEvent::Hide) {
          settle();
          // Re-arm: the next Show of this SAME menu plays (and lands) again.
          played_ = false;
          settled_ = false;
        }
        return QObject::eventFilter(watched, event);
      }

     private:
      void play() {
        QMenu* m = menu_;
        // exec()/popup() has already placed the popup by Show time.
        target_ = m->geometry();
        if (!target_.isValid()) return;
        const QPoint origin = origin_();
        // Sand first; the grow-from-the-cursor pop below is what plays when it declines.
        if (dustMenuIn(m, origin)) { settled_ = true; return; }
        // Start box: ~1/3 size, keeping the click point at the same fractional
        // spot it has in the final rect, so the growth radiates from the cursor.
        const QPoint a(qBound(target_.left(), origin.x(), target_.right()),
                       qBound(target_.top(), origin.y(), target_.bottom()));
        const int w = qMax(target_.width() / 3, 24);
        const int h = qMax(target_.height() / 3, 24);
        const double fx = double(a.x() - target_.left()) / qMax(target_.width(), 1);
        const double fy = double(a.y() - target_.top()) / qMax(target_.height(), 1);
        const QRect start(QPoint(a.x() - int(fx * w), a.y() - int(fy * h)), QSize(w, h));
        // Lift the min-size for the flight (the chat menu is fixed-width, and
        // setGeometry clamps to constraints); settle() puts it back.
        savedMin_ = m->minimumSize();
        m->setMinimumSize(1, 1);
        m->setWindowOpacity(0.0);
        m->setGeometry(start);

        auto* geo = new QPropertyAnimation(m, "geometry", this);
        geo->setDuration(kMenuMs);
        geo->setStartValue(start);
        geo->setEndValue(target_);
        geo->setEasingCurve(QEasingCurve::OutCubic);
        auto* fade = new QPropertyAnimation(m, "windowOpacity", this);
        fade->setDuration(kMenuMs);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        group_ = new QParallelAnimationGroup(this);
        group_->addAnimation(geo);
        group_->addAnimation(fade);
        connect(group_, &QParallelAnimationGroup::finished, this, [this] { settle(); });
        group_->start(QAbstractAnimation::DeleteWhenStopped);
      }

      // Land at the real box and undo everything play() overrode. Idempotent.
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
      bool played_ = false;
      bool settled_ = false;
    };

    // Backstop for Qt's own submenu-closing heuristic: closes `sub` once the pointer
    // has settled on a different row of `parent`, off hovered() rather than native
    // mouse-move delivery (which a WA_TranslucentBackground popup can lose track of).
    // Always on — this is correctness, not decoration.
    class SubmenuCloseGuard : public QObject {
     public:
      SubmenuCloseGuard(QMenu* sub, QMenu* parent, QAction* parentAction)
          : QObject(sub), sub_(sub), parent_(parent), parentAction_(parentAction) {
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, [this] {
          if (!sub_ || !sub_->isVisible()) return;
          // The pointer may have already reached `sub` via a diagonal move that
          // grazed a sibling row — check geometry, not Enter/Leave delivery.
          if (sub_->geometry().contains(QCursor::pos())) return;
          // Dust BEFORE hide(): a hide we trigger already reports isVisible()==false
          // by the time aboutToHide fires, so dustMenuOut off that signal would find
          // nothing left to photograph.
          if (dustMotionOk() && parent_ && parentAction_) {
            const QRect row = parent_->actionGeometry(parentAction_);
            if (row.isValid())
              dustMenuOut(sub_, parent_->mapToGlobal(row.center()));
          }
          sub_->hide();
        });
        connect(parent, &QMenu::hovered, this, [this](QAction* a) {
          if (a == parentAction_) { timer_.stop(); lastHovered_ = a; return; }
          // hovered(QAction*) can re-fire for the action already being hovered (Qt's
          // own sloppy-hover bookkeeping); only a genuinely new row restarts the clock.
          if (a == lastHovered_) return;
          lastHovered_ = a;
          timer_.start(graceMs());
        });
        // A click, Escape, or the parent closing outright must not leave this timer
        // armed to fire against a `sub` that (or whose parent) is already gone.
        connect(parent, &QMenu::aboutToHide, this, [this] { timer_.stop(); });
      }

     private:
      // Grace before this backstop assumes a stray hover means the pointer left for good.
      // A submenu flipped LEFT (no room on the right) sits across the parent's whole
      // width instead of one short hop — a real mouse takes longer to cross that, so it
      // needs a longer grace than the right-opening case.
      int graceMs() const {
        if (!sub_ || !parent_ || !parentAction_) return 220;
        // actionGeometry() is parent-local; sub_->geometry() is global (its own popup).
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
    };

    // A menu owned by a button is built ONCE and popped many times, so its flight cannot
    // be the one-shot MenuReveal above. This one plays on every Show and pours the motes
    // back into the same control on the way out. Both halves re-check reduced motion and
    // the anchor's own visibility, so a dock that closed under it simply stops flying.
    class MenuFlight : public QObject {
     public:
      MenuFlight(QMenu* menu, QWidget* anchor) : QObject(menu), menu_(menu), anchor_(anchor) {
        // Named so a test can see the wiring: the flight itself is skipped offscreen
        // (revealPopup), which is exactly where the gui suite runs.
        setObjectName(QStringLiteral("stencilMenuFlight"));
        menu->installEventFilter(this);
        // aboutToHide, NOT QEvent::Hide: the popup has to still be on screen to be
        // photographed. The cloud is a copy, so the menu itself still goes at once.
        QObject::connect(menu, &QMenu::aboutToHide, this, [this] {
          if (menu_ && anchor_) dismissPopup(*menu_, anchor_, kMenuPopupDustMs);
        });
      }

     protected:
      bool eventFilter(QObject* watched, QEvent* event) override {
        // popup() has placed and sized the menu by Show time — before that there is
        // nothing to grab (the same point MenuReveal::play relies on).
        if (watched == menu_ && event->type() == QEvent::Show && menu_ && anchor_)
          revealPopup(*menu_, anchor_, kMenuPopupDustMs);
        return QObject::eventFilter(watched, event);
      }

     private:
      QPointer<QMenu> menu_;
      QPointer<QWidget> anchor_;
    };
  }  // namespace

  namespace {
    // Where a popup's motes stream out of / pour back into. For a COMBO that is the
    // caret at its right edge — the arrow the user actually pressed (browser
    // dropdownMenu.js dustPoint parity) — clamped to the centre for a control too
    // narrow to have a distinct arrow zone; everything else keeps its centre.
    QPoint popupOriginGlobal(QWidget* anchor) {
      const QRect r = anchor->rect();
      if (qobject_cast<QComboBox*>(anchor))
        return anchor->mapToGlobal(
            QPoint(std::max(r.center().x(), r.right() - 14), r.center().y()));
      return anchor->mapToGlobal(r.center());
    }
  }  // namespace

  // The list a QComboBox drops: not a QMenu, and it places itself, so there is nothing
  // to grow — only the sand, streaming out of the control that owns it.
  bool revealPopup(QWidget& popup, QWidget* anchor, int ms) {
    if (!dustMotionOk()) return false;
    if (!anchor || !anchor->isVisible()) return false;
    QWidget* host = anchor->window();
    return dustPopupIn(&popup, host, popupOriginGlobal(anchor), ms);
  }

  bool dismissPopup(QWidget& popup, QWidget* anchor, int ms) {
    if (!dustMotionOk()) return false;
    if (!anchor || !anchor->isVisible()) return false;
    // No popup.isVisible() gate: a Qt::Popup Qt closed itself (an outside click) is
    // already hidden by the time its own Hide event tells a caller about it, and
    // grab() still renders it correctly — see the header comment.
    return dustPopupOut(&popup, anchor->window(), popupOriginGlobal(anchor), ms);
  }

  void revealMenu(QMenu& menu, const QPoint& origin) {
    // Offscreen has no compositor for windowOpacity, and the gui tests pick
    // items the instant the popup lands — both want the plain pop.
    if (!dustMotionOk()) return;
    new MenuReveal(&menu, [origin] { return origin; });  // owned by the menu
  }

  void revealSubmenu(QMenu& sub, QMenu& parent, QAction& parentAction) {
    // Correctness, not decoration — wired regardless of reduced motion/offscreen.
    new SubmenuCloseGuard(&sub, &parent, &parentAction);   // owned by sub
    if (!dustMotionOk()) return;
    QPointer<QMenu> parentGuard(&parent);
    QPointer<QAction> actionGuard(&parentAction);
    new MenuReveal(&sub, [parentGuard, actionGuard] {
      if (!parentGuard || !actionGuard) return QCursor::pos();
      const QRect row = parentGuard->actionGeometry(actionGuard);
      return row.isValid() ? parentGuard->mapToGlobal(row.center()) : QCursor::pos();
    });
  }

  void revealMenuBarMenu(QMenu& menu, QMenuBar& bar) {
    if (!dustMotionOk()) return;
    // Natively drawn (macOS global bar, GNOME/Unity appmenu): there is no Qt-rendered
    // popup on screen to grab or fly, only the OS's own menu.
    if (bar.isNativeMenuBar()) return;
    QPointer<QMenuBar> barGuard(&bar);
    QPointer<QMenu> menuGuard(&menu);
    new MenuReveal(&menu, [barGuard, menuGuard] {
      if (!barGuard || !menuGuard) return QCursor::pos();
      const QRect cell = barGuard->actionGeometry(menuGuard->menuAction());
      return cell.isValid() ? barGuard->mapToGlobal(cell.center()) : QCursor::pos();
    });
  }

  void revealMenuFrom(QMenu& menu, QWidget* anchor) {
    if (!anchor) return;
    // No reduced-motion / offscreen gate here: the filter is wired once but decides per
    // show, and revealPopup/dismissPopup already refuse in both cases.
    new MenuFlight(&menu, anchor);  // owned by the menu
  }

}  // namespace stencil::support
