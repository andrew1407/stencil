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

  bool StayOpenMenu::eventFilter(QObject* watched, QEvent* event) {
    // ↑/↓ on a radio row picks the neighbour: QAbstractButton only clicks it when the one leaving
    // was checked.
    if (event->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(event);
      // Tab on a focused control is our walk, not QWidget's focus chain (a spinner swallowed it).
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

  // One Tab step, wrapping, bridging into the flyout's plain rows too. False when the menu hosts
  // no controls.
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
      // A drag that wandered off the panel still ends on its target, or the splitter stays
      // latched.
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

