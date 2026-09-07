#include "stayOpenMenu.hpp"

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

  // The hosted controls Tab walks, in row order: every focusable widget inside this
  // menu's QWidgetAction rows (the Style spinners, the formula inputs, the tooltip
  // checkboxes, the chat's input and buttons), skipping hidden or disabled ones.
  namespace {
    // The focusable widgets inside one hosted row, in child order.
    QList<QWidget*> stopsIn(QWidget* host) {
      QList<QWidget*> stops;
      QList<QWidget*> candidates = host->findChildren<QWidget*>();
      candidates.prepend(host);
      for (QWidget* w : candidates) {
        // A spinner's inner line edit proxies focus to the spinner: one stop, not two.
        if (w->focusProxy()) continue;
        if (!(w->focusPolicy() & Qt::TabFocus) || !w->isEnabled() || !w->isVisibleTo(host)) continue;
        stops.append(w);
      }
      return stops;
    }

    // A hosted row with nothing to focus — the "IMAGE" / "LINE STYLE" section titles.
    // Qt's keyboard walk treats one as a row, and the highlight then goes nowhere.
    bool isLabelRow(QAction* a) {
      auto* wa = qobject_cast<QWidgetAction*>(a);
      return wa && wa->defaultWidget() && stopsIn(wa->defaultWidget()).isEmpty();
    }
  }  // namespace

  QList<QWidget*> StayOpenMenu::tabStops() const {
    QList<QWidget*> stops;
    for (QAction* a : actions()) {
      auto* wa = qobject_cast<QWidgetAction*>(a);
      if (!wa || !wa->defaultWidget() || !a->isVisible()) continue;
      stops += stopsIn(wa->defaultWidget());
    }
    return stops;
  }

  // The first row the keyboard can land on: enabled, visible, not a separator and not
  // a hosted row (a title, or a control row — those are entered, not highlighted).
  QList<QAction*> StayOpenMenu::rowActions() const {
    QList<QAction*> rows;
    for (QAction* a : actions()) {
      if (a->isSeparator() || !a->isEnabled() || !a->isVisible()) continue;
      if (qobject_cast<QWidgetAction*>(a)) continue;
      rows.append(a);
    }
    return rows;
  }

  QAction* StayOpenMenu::firstRowAction() const {
    const QList<QAction*> rows = rowActions();
    return rows.isEmpty() ? nullptr : rows.first();
  }

  // Focus one hosted control, installing this menu as its event filter once so a radio's
  // ↑/↓ can be taken over below.
  void StayOpenMenu::focusStop(QWidget* w, Qt::FocusReason reason) {
    if (!w) return;
    if (!filteredStops_.contains(w)) {
      w->installEventFilter(this);
      filteredStops_.insert(w);
      connect(w, &QObject::destroyed, this, [this, w] { filteredStops_.remove(w); });
    }
    w->setFocus(reason);
  }

  bool StayOpenMenu::eventFilter(QObject* watched, QEvent* event) {
    // ↑/↓ on a focused radio row (the Image Filter group): move to the group's next radio
    // and PICK it — QAbstractButton only clicks the neighbour when the one leaving was
    // checked, which left the keys merely moving focus.
    if (event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      // Tab on a focused control is OUR walk, not QWidget's focus chain (a spinner
      // swallowed it and the keys never left the point-size box).
      if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
        const bool back = ke->key() == Qt::Key_Backtab || (ke->modifiers() & Qt::ShiftModifier);
        if (walkTab(back)) return true;
      }
      auto* radio = qobject_cast<QRadioButton*>(watched);
      QButtonGroup* group = radio ? radio->group() : nullptr;
      if (group && (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down)) {
        QList<QAbstractButton*> radios;   // the group's own order, minus what cannot be reached
        for (QAbstractButton* b : group->buttons())
          if (b->isEnabled() && b->isVisible()) radios.append(b);
        const int at = radios.indexOf(radio);
        const int n = radios.size();
        if (at >= 0 && n > 1) {
          const bool down = ke->key() == Qt::Key_Down;
          QAbstractButton* next = radios[(at + (down ? 1 : -1) + n) % n];
          focusStop(next, down ? Qt::TabFocusReason : Qt::BacktabFocusReason);
          next->click();
          return true;
        }
      }
    }
    return QMenu::eventFilter(watched, event);
  }

  bool StayOpenMenu::enterControls() {
    QWidget* target = keyTarget_ && keyTarget_->isVisible() && keyTarget_->isEnabled() ? keyTarget_ : nullptr;
    if (!target) {
      const QList<QWidget*> stops = tabStops();
      if (!stops.isEmpty()) target = stops.first();
    }
    if (!target) return false;
    entered_ = true;
    focusStop(target, Qt::TabFocusReason);
    return true;
  }

  // One Tab step over the hosted controls (wrapping). The walk bridges into the
  // flyout's plain rows too (Style: spinners, then Solid / Dashed / Dotted): Tab off
  // the last control lands on the first row, Shift+Tab off the first control on the
  // last, ↑/↓ then walk (and apply) the rows, and Tab from a row goes back to the
  // first control. False when the menu hosts no controls (QMenu's default then).
  bool StayOpenMenu::walkTab(bool back) {
    const QList<QWidget*> stops = tabStops();
    if (stops.isEmpty()) return false;
    entered_ = true;
    const int at = stops.indexOf(QApplication::focusWidget());
    const int n = stops.size();
    const QList<QAction*> rows = rowActions();
    if (!rows.isEmpty() && at >= 0 && ((!back && at == n - 1) || (back && at == 0))) {
      stops[at]->clearFocus();   // the menu takes the keys back from the control
      setActiveAction(back ? rows.last() : rows.first());
      return true;
    }
    const int next = at < 0 ? (back ? n - 1 : 0) : (at + (back ? -1 : 1) + n) % n;
    focusStop(stops[next], back ? Qt::BacktabFocusReason : Qt::TabFocusReason);
    return true;
  }

  void StayOpenMenu::keyPressEvent(QKeyEvent* e) {
    // The keyboard can stay with THIS menu while a control inside one of its flyouts has
    // focus (a hover-opened flyout never takes the grab). Hand the key to the control
    // exactly as Qt would if the flyout held the keys; Escape stays here.
    if (!redispatchingKey_ && e->key() != Qt::Key_Escape) {
      QWidget* fw = QApplication::focusWidget();
      auto* owner = fw ? qobject_cast<StayOpenMenu*>(fw->window()) : nullptr;
      bool descendant = false;
      for (QWidget* w = owner ? owner->parentWidget() : nullptr; w && !descendant; w = w->parentWidget())
        descendant = w == this;
      if (owner && owner != this && fw != owner && owner->isVisible() && descendant) {
        const Latch latch(redispatchingKey_);
        QApplication::sendEvent(fw, e);
        return;
      }
    }
    // → on a parent row reveals its flyout (Qt); a SECOND → enters it — onto the chat's
    // input, a Style spinner, the Transformation checkbox… — while ← still folds it
    // back. Two routes, since the keyboard can be held by the parent (its row still
    // current, the flyout already up) or by the flyout itself (Qt moved it there).
    if (e->key() == Qt::Key_Right && !(e->modifiers() & ~Qt::KeypadModifier)) {
      QAction* cur = activeAction();
      QMenu* open = cur && cur->menu() && cur->menu()->isVisible() ? cur->menu() : nullptr;
      if (auto* sub = qobject_cast<StayOpenMenu*>(open)) {
        if (!sub->enterControls()) {
          if (QAction* a = sub->firstRowAction()) sub->setActiveAction(a);
        }
        e->accept();
        return;
      }
      if (qobject_cast<QMenu*>(parentWidget()) && !(cur && cur->menu())) {
        // Inside a flyout on a plain row: enter its controls if it has any, else land
        // on its first real row. Never Qt's default here, which reads → on such a row
        // as ← and closes the flyout.
        entered_ = true;
        if (!enterControls()) {
          if (QAction* a = firstRowAction()) setActiveAction(a);
        }
        e->accept();
        return;
      }
    }
    // A flyout the first → only REVEALED still belongs to its parent's walk: ↑/↓ go back
    // to the parent, which moves its highlight and folds this flyout (browser parity —
    // the highlight stays on the parent row until the second →).
    if (!entered_ && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
      if (auto* parent = qobject_cast<QMenu*>(parentWidget()); parent && parent->isVisible()) {
        QApplication::sendEvent(parent, e);
        return;
      }
    }
    // ↑/↓ never rest on a title row: QMenu's own walk stops there (an invisible
    // highlight, keys that seem dead) — step on past it in the same direction.
    if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
      QMenu::keyPressEvent(e);
      const int rowCap = actions().size();   // a walk of every row is the most it can take
      for (int i = 0; i < rowCap && isLabelRow(activeAction()); ++i) {
        QKeyEvent again(QEvent::KeyPress, e->key(), Qt::NoModifier);
        QMenu::keyPressEvent(&again);
      }
      // Landing on one option of an exclusive group (Style's Solid / Dashed / Dotted)
      // picks it at once — browser parity, where arrowing a radio group applies the
      // option as the focus moves. trigger() runs the row's handler without QMenu's
      // own activate path, so the menu stays open.
      if (QAction* a = activeAction(); a && a->isCheckable() && !a->isChecked()) {
        if (QActionGroup* g = a->actionGroup(); g && g->isExclusive()) a->trigger();
      }
      return;
    }
    // Tab / Shift+Tab walk the hosted controls (wrapping) instead of QMenu's default,
    // which reads Tab as ↓ and never reaches a spinner, input or checkbox inside a
    // flyout. A menu with no controls keeps the default.
    if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
      const bool back = e->key() == Qt::Key_Backtab || (e->modifiers() & Qt::ShiftModifier);
      if (walkTab(back)) { e->accept(); return; }
    }
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
