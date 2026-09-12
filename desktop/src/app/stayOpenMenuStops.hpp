#pragma once
// The focusable stops inside one hosted menu row, private to the StayOpenMenu*.cpp TUs.
#include <QAction>
#include <QList>
#include <QWidget>
#include <QWidgetAction>

namespace stencil::gui {

  inline QList<QWidget*> stopsIn(QWidget* host) {
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

  // A section title row: Qt's keyboard walk treats it as a row and the highlight goes nowhere.
  inline bool isLabelRow(QAction* a) {
    auto* wa = qobject_cast<QWidgetAction*>(a);
    return wa && wa->defaultWidget() && stopsIn(wa->defaultWidget()).isEmpty();
  }

}  // namespace stencil::gui
