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

  // Every focusable widget inside this menu's QWidgetAction rows, skipping hidden or disabled
  // ones.

  QList<QWidget*> StayOpenMenu::tabStops() const {
    QList<QWidget*> stops;
    for (QAction* a : actions()) {
      auto* wa = qobject_cast<QWidgetAction*>(a);
      if (!wa || !wa->defaultWidget() || !a->isVisible()) continue;
      stops += stopsIn(wa->defaultWidget());
    }
    return stops;
  }

  // Not a hosted row (a title or a control row — those are entered, not highlighted).
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

  // The filter is installed once so a radio's ↑/↓ can be taken over below.
  void StayOpenMenu::focusStop(QWidget* w, Qt::FocusReason reason) {
    if (!w) return;
    if (!filteredStops.contains(w)) {
      w->installEventFilter(this);
      filteredStops.insert(w);
      connect(w, &QObject::destroyed, this, [this, w] { filteredStops.remove(w); });
    }
    w->setFocus(reason);
  }

  void StayOpenMenu::keyPressEvent(QKeyEvent* e) {
    // A hover-opened flyout never takes the grab, so the key is handed to its focused control as
    // Qt would; Escape stays here.
    if (!redispatchingKey && e->key() != Qt::Key_Escape) {
      QWidget* fw = QApplication::focusWidget();
      auto* owner = fw ? qobject_cast<StayOpenMenu*>(fw->window()) : nullptr;
      bool descendant = false;
      for (QWidget* w = owner ? owner->parentWidget() : nullptr; w && !descendant; w = w->parentWidget())
        descendant = w == this;
      if (owner && owner != this && fw != owner && owner->isVisible() && descendant) {
        const Latch latch(redispatchingKey);
        QApplication::sendEvent(fw, e);
        return;
      }
    }
    // → on a parent row reveals its flyout (Qt); a second → enters it. Two routes: the parent or
    // the flyout may hold the keyboard.
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
        // Never Qt's default here, which reads → on such a row as ← and closes the flyout.
        entered = true;
        if (!enterControls()) {
          if (QAction* a = firstRowAction()) setActiveAction(a);
        }
        e->accept();
        return;
      }
    }
    // A flyout the first → only revealed still belongs to its parent's walk (browser parity).
    if (!entered && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
      if (auto* parent = qobject_cast<QMenu*>(parentWidget()); parent && parent->isVisible()) {
        QApplication::sendEvent(parent, e);
        return;
      }
    }
    // QMenu's own walk stops on a title row with an invisible highlight; step past it.
    if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
      QMenu::keyPressEvent(e);
      const int rowCap = actions().size();   // a walk of every row is the most it can take
      for (int i = 0; i < rowCap && isLabelRow(activeAction()); ++i) {
        QKeyEvent again(QEvent::KeyPress, e->key(), Qt::NoModifier);
        QMenu::keyPressEvent(&again);
      }
      // Arrowing an exclusive group applies the option (browser parity); trigger() bypasses
      // QMenu's activate path so the menu stays open.
      if (QAction* a = activeAction(); a && a->isCheckable() && !a->isChecked()) {
        if (QActionGroup* g = a->actionGroup(); g && g->isExclusive()) a->trigger();
      }
      return;
    }
    // QMenu reads Tab as ↓ and never reaches a control inside a flyout; a menu with no controls
    // keeps the default.
    if ((e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) &&
        !keepsTab(QApplication::focusWidget())) {
      const bool back = e->key() == Qt::Key_Backtab || (e->modifiers() & Qt::ShiftModifier);
      if (walkTab(back)) { e->accept(); return; }
    }
    // Escape always belongs to the menu. Same latch as the mouse path.
    if (keyTarget && !redispatchingKey && e->key() != Qt::Key_Escape) {
      QWidget* fw = focusWidget();  // this menu's own focus widget
      const bool ours = fw && fw != this && fw->window() == window() &&
                        (fw == keyTarget || keyTarget->isAncestorOf(fw));
      if (ours) {
        const Latch latch(redispatchingKey);
        QApplication::sendEvent(fw, e);  // typing, Enter, arrows — the input's
        return;
      }
    }
    QMenu::keyPressEvent(e);
  }
}  // namespace stencil::gui

