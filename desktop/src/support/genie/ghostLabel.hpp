#pragma once
// A picture flown as a scaled label over its host, placed by the caller at each step of one 0–1 run: the chat
// card's tail grow and shrink, the popover's scaled flight. Browser twin: the transforms of chatGrowIn and
// modalFromIcon / modalToIcon (animations/overlay/overlays.css).
#include <QEasingCurve>
#include <QLabel>
#include <QPixmap>
#include <QRect>
#include <QWidget>

#include <functional>

namespace stencil::support::genie {

  // `shot` scaled to whatever box the label is given, over `host`, taking no pointer; shown by its flight.
  QLabel* makeGhostLabel(QWidget* host, const QPixmap& shot);
  // Runs `place(ghost, t)` from 0 to 1 over `ms` on `curve`, then `done`, and the ghost is gone.
  void flyGhostLabel(QLabel* ghost, int ms, QEasingCurve::Type curve, std::function<void(QLabel&, double)> place,
                     std::function<void()> done = {});
  // `box` scaled by `s` into its bottom corner, on the right side when `isRight`, else the left: a card's tail.
  QRect tailBox(const QRect& box, double s, bool isRight);

}  // namespace stencil::support::genie
