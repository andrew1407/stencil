// A list row's leave and arrival at its list's left edge: Sliding pours the row's picture into (or out of)
// a strip there along the genie's funnel, Transition slides it; the viewport clips both.
// Browser twin: js/ui/projects/list/rowMotion.js (leftHome, rowLeave, rowArrive).
#pragma once

#include <QPixmap>
#include <QRect>
#include <QWidget>

#include <functional>

namespace stencil::gui {

  // How long the picture flies, ms before the speed: Sliding's list pour, else the row clock.
  int edgeFlightMs();

  // `r` is `view`-local. False, with nothing shown, outside Sliding and Transition.
  bool edgeAway(QWidget* view, const QPixmap& shot, const QRect& r);
  bool edgeIn(QWidget* view, const QPixmap& shot, const QRect& r, std::function<void()> landed);

}  // namespace stencil::gui
