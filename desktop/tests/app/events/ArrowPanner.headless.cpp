// The held arrows' pan (app/events/ArrowPanner): each tick pans by the SUM of the arrows held, so
// two make a diagonal; Shift is the fast step; a release drops one arrow, a focus loss all of them,
// and the tick stops its timer once nothing is held.
#include "ArrowPanner.hpp"

#include <QCoreApplication>
#include <QPoint>
#include <QVector>
#include <cstdio>

#include "../../support/check.hpp"

using stencil::gui::ArrowPanner;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QVector<QPoint> pans;
  ArrowPanner pan(nullptr, [&pans](int dx, int dy) { pans << QPoint(dx, dy); });
  check(!pan.isPanning(), "nothing held, no tick");

  pan.press(-1, 0, false);
  check(pan.isPanning(), "an arrow starts the tick");
  pan.tick();
  check(pans.value(0) == QPoint(-7, 0), "left pans 7 px a tick");
  pan.press(0, 1, false);
  pan.tick();
  check(pans.value(1) == QPoint(-7, 7), "left + down pan diagonally");
  pan.setShift(true);
  pan.tick();
  check(pans.value(2) == QPoint(-22, 22), "Shift is the fast step");
  pan.release(Qt::Key_Shift);
  pan.release(Qt::Key_Left);
  pan.tick();
  check(pans.value(3) == QPoint(0, 7), "a released arrow stops counting, and so does Shift");

  pan.press(1, 0, true);
  pan.tick();
  check(pans.value(4) == QPoint(22, 22), "an arrow pressed with Shift held makes every held arrow fast");
  pan.press(-1, 0, true);
  pan.tick();
  check(pans.value(5) == QPoint(0, 22), "left and right cancel out");

  pan.stop();
  check(!pan.isPanning(), "a focus loss stops the tick");
  const int before = pans.size();
  pan.tick();
  check(pans.size() == before, "and nothing is held to pan by");

  pan.press(0, -1, false);
  pan.release(Qt::Key_Up);
  pan.tick();
  check(!pan.isPanning() && pans.size() == before, "the tick after the last release pans nothing and stops");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
