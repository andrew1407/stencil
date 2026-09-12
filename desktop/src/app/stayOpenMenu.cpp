#include "stayOpenMenu.hpp"
#include "stayOpenMenuStops.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QKeyEvent>
#include <QActionEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QButtonGroup>
#include <QRadioButton>
#include <QTimer>
#include <QShowEvent>
#include <QPointer>
#include <QWidgetAction>

namespace stencil::gui {

  StayOpenMenu::StayOpenMenu(QWidget* parent) : QMenu(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
  }
  StayOpenMenu::StayOpenMenu(const QString& title, QWidget* parent) : QMenu(title, parent) {
    setAttribute(Qt::WA_TranslucentBackground);
  }

  void StayOpenMenu::setInteractiveArea(QWidget* area, QWidget* keyTarget) {
    area_ = area;
    keyTarget_ = keyTarget;
    // CRITICAL: stop mouse/key propagation at the panel — an unaccepted event
    // re-dispatched to a child would bubble back to this menu and recurse until
    // the stack blew up.
    if (area_) area_->setAttribute(Qt::WA_NoMousePropagation, true);
  }

  QAbstractButton* StayOpenMenu::toggleAt(const QPoint& p) {
    for (QWidget* c = childAt(p); c && c != this; c = c->parentWidget()) {
      if (auto* b = qobject_cast<QAbstractButton*>(c)) return b;
      if (auto* b = c->findChild<QAbstractButton*>()) return b;
    }
    return nullptr;
  }

  // A checkable, enabled plain action under the cursor that should toggle in
  // place instead of dismissing the menu.
  QAction* StayOpenMenu::checkableAt(const QPoint& p) {
    QAction* a = actionAt(p);
    return (a && a->isCheckable() && a->isEnabled()) ? a : nullptr;
  }

  // A STRICT descendant of the interactive area under `p`. Deliberately narrow:
  // a re-dispatch can never target the menu or an ancestor of it (the shape of
  // the stack-overflow crash), and points outside the panel reach QMenu untouched.
  QWidget* StayOpenMenu::strictChildInArea(const QPoint& p) const {
    if (!area_ || !area_->isVisible()) return nullptr;
    if (!area_->geometry().contains(p)) return nullptr;
    QWidget* c = childAt(p);
    if (!c || c == this || c == area_) return nullptr;
    if (!area_->isAncestorOf(c)) return nullptr;
    if (c->window() != window()) return nullptr;  // never cross windows
    return c;
  }

  // Hand a mouse event inside the interactive area to the real child. Must run
  // BEFORE toggleAt(), whose findChild() fallback would otherwise match the
  // chat's own send button for a click anywhere in the panel.
  bool StayOpenMenu::deliverToArea(QMouseEvent* e) {
    if (!area_ || !area_->isVisible()) return false;
    const QPoint p = e->position().toPoint();
    if (!area_->geometry().contains(p)) return false;  // not ours — hands off
    // Re-entrancy latch: a bounced re-dispatched event dies here instead of
    // being re-dispatched again.
    if (redispatching_) {
      e->accept();
      return true;
    }
    QWidget* child = strictChildInArea(p);
    // Inside the panel but not on a re-dispatchable descendant: swallow so the
    // menu stays open, send nothing.
    if (!child) {
      e->accept();
      return true;
    }
    // A button in the panel behaves like the menu's other hosted controls:
    // clicked on release, never closing the menu.
    for (QWidget* w = child; w && w != area_; w = w->parentWidget()) {
      if (auto* b = qobject_cast<QAbstractButton*>(w)) {
        if (e->type() == QEvent::MouseButtonRelease && b->isEnabled()) b->click();
        e->accept();
        return true;
      }
    }
    // Everything else (the input, the transcript) gets the real event — plus
    // focus on press, which makes typing land in the input rather than the menu.
    if (e->type() == QEvent::MouseButtonPress && keyTarget_ &&
        (child == keyTarget_ || keyTarget_->isAncestorOf(child)))
      keyTarget_->setFocus(Qt::MouseFocusReason);
    // Remember the press target so the drag that follows (the splitter handle)
    // keeps receiving moves — QMenu owns the grab.
    if (e->type() == QEvent::MouseButtonPress) pressTarget_ = child;
    if (e->type() == QEvent::MouseButtonRelease) pressTarget_ = nullptr;
    forward(child, e);
    e->accept();
    return true;
  }

  // Re-send `e` to `target` in its own coordinates, under the latch.
  void StayOpenMenu::forward(QWidget* target, QMouseEvent* e) {
    const QPointF local = target->mapFrom(this, e->position().toPoint());
    QMouseEvent copy(e->type(), local, e->scenePosition(), e->globalPosition(),
                     e->button(), e->buttons(), e->modifiers());
    const Latch latch(redispatching_);
    QApplication::sendEvent(target, &copy);
  }

  // A drag started inside the panel owns the moves until the button comes back
  // up; everything else is QMenu's, so hover-opening submenus is untouched.
  void StayOpenMenu::mouseMoveEvent(QMouseEvent* e) {
    entered_ = true;   // the pointer coming in is as good as the second →
    if (pressTarget_ && !redispatching_ && (e->buttons() & Qt::LeftButton)) {
      forward(pressTarget_, e);
      e->accept();
      return;
    }
    // The popup grab means hosted widgets never get their own enter/leave, so
    // synthesise hover for the child under the cursor. Enter/Leave don't
    // propagate, so this cannot recurse.
    updateAreaHover(e);
    QMenu::mouseMoveEvent(e);
  }

  void StayOpenMenu::updateAreaHover(QMouseEvent* e) {
    QWidget* now = strictChildInArea(e->position().toPoint());
    if (now == hoverChild_) return;
    clearAreaHover();
    hoverChild_ = now;
    if (!now) return;
    const QPointF local = now->mapFrom(this, e->position().toPoint());
    QEnterEvent enter(local, e->scenePosition(), e->globalPosition());
    QApplication::sendEvent(now, &enter);
  }

  void StayOpenMenu::clearAreaHover() {
    if (!hoverChild_) return;
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(hoverChild_, &leave);
    hoverChild_ = nullptr;
  }

  // The cursor must never stay changed once the pointer (or the menu) is gone.
  void StayOpenMenu::leaveEvent(QEvent* e) {
    clearAreaHover();
    QMenu::leaveEvent(e);
  }

  void StayOpenMenu::hideEvent(QHideEvent* e) {
    clearAreaHover();
    QMenu::hideEvent(e);
  }

  // A row shown or hidden while the popup is up (the Custom Tint pick reveals "Tint
  // Color…") makes QMenu resize in place with its top-left pinned, so a flyout on the
  // screen's bottom edge grows straight off it. Re-place it once the resize has landed.
  void StayOpenMenu::actionEvent(QActionEvent* e) {
    QMenu::actionEvent(e);
    if (!isVisible()) return;
    QPointer<StayOpenMenu> self(this);
    QTimer::singleShot(0, this, [self] {
      if (!self || !self->isVisible() || !self->screen()) return;
      const QRect avail = self->screen()->availableGeometry();
      QRect g = self->geometry();
      if (g.bottom() > avail.bottom()) g.moveBottom(avail.bottom());
      if (g.right() > avail.right()) g.moveRight(avail.right());
      if (g.top() < avail.top()) g.moveTop(avail.top());
      if (g.left() < avail.left()) g.moveLeft(avail.left());
      if (g.topLeft() != self->pos()) self->move(g.topLeft());
    });
  }

  // Opened from the keyboard, QMenu hands focus to its first hosted widget right after the
  // show — but the first → only REVEALS a flyout; the SECOND → enters it (keyPressEvent).
  // Deferred one tick: Qt's focus move lands after this event.
  void StayOpenMenu::showEvent(QShowEvent* e) {
    QMenu::showEvent(e);
    entered_ = false;   // revealed, not entered — see keyPressEvent's ↑/↓ hand-back
    QPointer<StayOpenMenu> self(this);
    QTimer::singleShot(0, this, [self] {
      if (!self || !self->isVisible()) return;
      QWidget* fw = QApplication::focusWidget();
      if (fw && fw != self && self->isAncestorOf(fw)) self->setFocus();
    });
  }
}  // namespace stencil::gui

