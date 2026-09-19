#pragma once
// Shared ground for the MainWindow tooltip GUI suites: finding a control whose tooltip is
// worth showing, and asking for it the way a hover does.
#include "MainWindow.gui.hpp"

namespace stencil::guitest {

  // A shown, enabled, tooltip-carrying TOOLBAR button. Searched per toolbar, not over the
  // whole window: a panel chevron retires itself moments after the window opens, and a
  // tooltip whose control went away is retired by the panel's anti-stranding heartbeat.
  inline QToolButton* tipCarrier(MainWindow& win) {
    for (QToolBar* bar : win.findChildren<QToolBar*>())
      for (QToolButton* b : bar->findChildren<QToolButton*>())
        if (b->isVisible() && b->isEnabled() && !b->toolTip().isEmpty()) return b;
    return nullptr;
  }

  inline void sendToolTipTo(QWidget* w) {
    const QPoint local(4, 4);
    QHelpEvent ev(QEvent::ToolTip, local, w->mapToGlobal(local));
    QApplication::sendEvent(w, &ev);
  }

  // A shown, enabled toolbar button whose rendered tooltip does (`want`) or does not carry
  // keycaps — the shake fires on the content, not on the control.
  inline QToolButton* capCarrier(MainWindow& win, bool want) {
    for (QToolBar* bar : win.findChildren<QToolBar*>())
      for (QToolButton* b : bar->findChildren<QToolButton*>()) {
        if (!b->isVisible() || !b->isEnabled() || b->toolTip().isEmpty()) continue;
        const QString rich = b->toolTip().trimmed().startsWith('<')
                                 ? b->toolTip()
                                 : stencil::gui::enrichedToolTip(b->toolTip());
        if (stencil::gui::hasKeycaps(rich) == want) return b;
      }
    return nullptr;
  }

}  // namespace stencil::guitest
