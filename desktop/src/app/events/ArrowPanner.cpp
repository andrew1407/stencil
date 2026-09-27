#include "ArrowPanner.hpp"

#include <QTimer>

// The held arrows: which are down, and the tick that pans by their sum.

namespace stencil::gui {

  ArrowPanner::ArrowPanner(QObject* parent, std::function<void(int dx, int dy)> panBy)
      : QObject(parent), panBy(std::move(panBy)), panTimer(new QTimer(this)) {
    panTimer->setInterval(16);
    connect(panTimer, &QTimer::timeout, this, &ArrowPanner::tick);
  }

  void ArrowPanner::press(int dirX, int dirY, bool shift) {
    if (dirX < 0) panLeft = true;
    else if (dirX > 0) panRight = true;
    if (dirY < 0) panUp = true;
    else if (dirY > 0) panDown = true;
    panShift = shift;
    if (panTimer && !panTimer->isActive()) panTimer->start();
  }

  void ArrowPanner::setShift(bool on) { panShift = on; }

  void ArrowPanner::release(int key) {
    switch (key) {
      case Qt::Key_Left: panLeft = false; break;
      case Qt::Key_Right: panRight = false; break;
      case Qt::Key_Up: panUp = false; break;
      case Qt::Key_Down: panDown = false; break;
      case Qt::Key_Shift: panShift = false; break;
      default: break;
    }
  }

  void ArrowPanner::stop() {
    panLeft = panRight = panUp = panDown = panShift = false;
    if (panTimer) panTimer->stop();
  }

  void ArrowPanner::tick() {
    if (!panLeft && !panRight && !panUp && !panDown) {
      panTimer->stop();
      return;
    }
    const int speed = panShift ? 22 : 7;
    int dx = 0, dy = 0;
    if (panLeft) dx -= 1;
    if (panRight) dx += 1;
    if (panUp) dy -= 1;
    if (panDown) dy += 1;
    if (dx || dy)
      panBy(dx * speed, dy * speed);
  }

  bool ArrowPanner::isPanning() const { return panTimer->isActive(); }

}  // namespace stencil::gui
