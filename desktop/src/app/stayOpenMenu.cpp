#include "stayOpenMenu.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

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

  void StayOpenMenu::keyPressEvent(QKeyEvent* e) {
    // Escape always belongs to the menu. Same latch as the mouse path: a
    // bounced key must never be re-forwarded.
    if (keyTarget_ && !redispatchingKey_ && e->key() != Qt::Key_Escape) {
      QWidget* fw = focusWidget();  // this menu's own focus widget
      const bool ours = fw && fw != this && fw->window() == window() &&
                        (fw == keyTarget_ || keyTarget_->isAncestorOf(fw));
      if (ours) {
        const Latch latch(redispatchingKey_);
        QApplication::sendEvent(fw, e);  // typing, Enter, arrows — the input's
        return;
      }
    }
    QMenu::keyPressEvent(e);
  }

  void StayOpenMenu::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
      if (deliverToArea(e)) return;
      if (toggleAt(e->pos()) || checkableAt(e->pos())) {
        e->accept();
        return;
      }
    }
    QMenu::mousePressEvent(e);
  }

  void StayOpenMenu::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
      // A drag that wandered off the panel still ends on ITS target, or the
      // splitter would stay latched to the cursor.
      if (pressTarget_ && !area_->geometry().contains(e->position().toPoint())) {
        QWidget* target = pressTarget_;
        pressTarget_ = nullptr;
        forward(target, e);
        e->accept();
        return;
      }
      if (deliverToArea(e)) return;
      if (QAbstractButton* b = toggleAt(e->pos())) {
        if (b->isEnabled()) b->click();   // checkbox toggles; radio checks
        e->accept();
        return;                           // do NOT call base → the menu stays open
      }
      if (QAction* a = checkableAt(e->pos())) {
        // Exclusive group → select (never uncheck); independent toggle → flip.
        if (QActionGroup* g = a->actionGroup(); g && g->isExclusive())
          a->setChecked(true);
        else
          a->toggle();
        e->accept();
        return;                           // keep the menu open
      }
    }
    QMenu::mouseReleaseEvent(e);
  }

}  // namespace stencil::gui
