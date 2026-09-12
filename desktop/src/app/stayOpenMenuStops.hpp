#pragma once
// The focusable stops inside one hosted menu row, private to the stayOpenMenu*.cpp TUs.
#include <QAction>
#include <QList>
#include <QWidget>
#include <QWidgetAction>

namespace stencil::gui {

  // The focusable widgets inside one hosted row, in child order.
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

  // A hosted row with nothing to focus — the "IMAGE" / "LINE STYLE" section titles.
  // Qt's keyboard walk treats one as a row, and the highlight then goes nowhere.
  inline bool isLabelRow(QAction* a) {
    auto* wa = qobject_cast<QWidgetAction*>(a);
    return wa && wa->defaultWidget() && stopsIn(wa->defaultWidget()).isEmpty();
  }

}  // namespace stencil::gui
