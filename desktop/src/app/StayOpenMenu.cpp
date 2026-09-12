#include "StayOpenMenu.hpp"
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
    // Stop propagation at the panel: an unaccepted event re-dispatched to a child bubbles back
    // here and recurses.
    if (area_) area_->setAttribute(Qt::WA_NoMousePropagation, true);
  }

  QAbstractButton* StayOpenMenu::toggleAt(const QPoint& p) {
    for (QWidget* c = childAt(p); c && c != this; c = c->parentWidget()) {
      if (auto* b = qobject_cast<QAbstractButton*>(c)) return b;
      if (auto* b = c->findChild<QAbstractButton*>()) return b;
    }
    return nullptr;
  }

  QAction* StayOpenMenu::checkableAt(const QPoint& p) {
    QAction* a = actionAt(p);
    return (a && a->isCheckable() && a->isEnabled()) ? a : nullptr;
  }

  // A strict descendant only: a re-dispatch can never target the menu or an ancestor (the stack-
  // overflow shape).
  QWidget* StayOpenMenu::strictChildInArea(const QPoint& p) const {
    if (!area_ || !area_->isVisible()) return nullptr;
    if (!area_->geometry().contains(p)) return nullptr;
    QWidget* c = childAt(p);
    if (!c || c == this || c == area_) return nullptr;
    if (!area_->isAncestorOf(c)) return nullptr;
    if (c->window() != window()) return nullptr;  // never cross windows
    return c;
  }

  // Must run before toggleAt(), whose findChild() fallback would match the chat's send button.
  bool StayOpenMenu::deliverToArea(QMouseEvent* e) {
    if (!area_ || !area_->isVisible()) return false;
    const QPoint p = e->position().toPoint();
    if (!area_->geometry().contains(p)) return false;  // not ours — hands off
    // Re-entrancy latch.
    if (redispatching_) {
      e->accept();
      return true;
    }
    QWidget* child = strictChildInArea(p);
    // Inside the panel but not on a re-dispatchable descendant: swallow so the menu stays open.
    if (!child) {
      e->accept();
      return true;
    }
    for (QWidget* w = child; w && w != area_; w = w->parentWidget()) {
      if (auto* b = qobject_cast<QAbstractButton*>(w)) {
        if (e->type() == QEvent::MouseButtonRelease && b->isEnabled()) b->click();
        e->accept();
        return true;
      }
    }
    // Focus on press, so typing lands in the input rather than the menu.
    if (e->type() == QEvent::MouseButtonPress && keyTarget_ &&
        (child == keyTarget_ || keyTarget_->isAncestorOf(child)))
      keyTarget_->setFocus(Qt::MouseFocusReason);
    // QMenu owns the grab, so the press target keeps receiving the drag's moves.
    if (e->type() == QEvent::MouseButtonPress) pressTarget_ = child;
    if (e->type() == QEvent::MouseButtonRelease) pressTarget_ = nullptr;
    forward(child, e);
    e->accept();
    return true;
  }

  void StayOpenMenu::forward(QWidget* target, QMouseEvent* e) {
    const QPointF local = target->mapFrom(this, e->position().toPoint());
    QMouseEvent copy(e->type(), local, e->scenePosition(), e->globalPosition(),
                     e->button(), e->buttons(), e->modifiers());
    const Latch latch(redispatching_);
    QApplication::sendEvent(target, &copy);
  }

  void StayOpenMenu::mouseMoveEvent(QMouseEvent* e) {
    entered_ = true;   // the pointer coming in is as good as the second →
    if (pressTarget_ && !redispatching_ && (e->buttons() & Qt::LeftButton)) {
      forward(pressTarget_, e);
      e->accept();
      return;
    }
    // The popup grab means hosted widgets never get enter/leave; synthesised here. Enter/Leave do
    // not propagate, so no recursion.
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

  void StayOpenMenu::leaveEvent(QEvent* e) {
    clearAreaHover();
    QMenu::leaveEvent(e);
  }

  void StayOpenMenu::hideEvent(QHideEvent* e) {
    clearAreaHover();
    QMenu::hideEvent(e);
  }

  // A row shown mid-popup makes QMenu grow with its top-left pinned, straight off the screen's
  // bottom edge; re-place after the resize.
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

  // From the keyboard, QMenu focuses its first hosted widget after the show; deferred one tick so
  // the first → only reveals a flyout.
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

