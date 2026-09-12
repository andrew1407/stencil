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

  // The hosted controls Tab walks, in row order: every focusable widget inside this
  // menu's QWidgetAction rows (the Style spinners, the formula inputs, the tooltip
  // checkboxes, the chat's input and buttons), skipping hidden or disabled ones.

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
}  // namespace stencil::gui

