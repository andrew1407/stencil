// Headless check of the genie warp (src/support/genie/genieWarp.hpp): the side the bands flow to, the
// window drawn whole at rest, the near edge leading into a funnel, and the window on the icon at the end.
#include "../../../src/support/genie/genieWarp.hpp"

#include <QCoreApplication>
#include <cmath>

using namespace stencil::support::genie;

#include "../../support/check.hpp"
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QRectF win(100, 200, 400, 300);   // x 100–500, y 200–500

  // ── The side: the icon's centre past an edge, the widest gap winning; over the window, its direction.
  check(sideOf(win, QRectF(280, 40, 24, 24)) == Side::UP, "an icon above is UP");
  check(sideOf(win, QRectF(280, 700, 24, 24)) == Side::DOWN, "an icon below is DOWN");
  check(sideOf(win, QRectF(800, 300, 24, 24)) == Side::RIGHT, "an icon to the right is RIGHT");
  check(sideOf(win, QRectF(10, 520, 24, 24)) == Side::LEFT, "the widest gap wins");
  check(sideOf(win, QRectF(280, 220, 24, 24)) == Side::UP, "an icon over the window flows the way it lies from the centre");

  for (Side side : {Side::DOWN, Side::UP, Side::RIGHT, Side::LEFT}) {
    const QRectF icon = side == Side::DOWN ? QRectF(280, 700, 24, 24)
                      : side == Side::UP ? QRectF(280, 40, 24, 24)
                      : side == Side::RIGHT ? QRectF(800, 300, 24, 24) : QRectF(-100, 300, 24, 24);

    // ── At rest: the strips tile the window exactly and the whole snapshot.
    const auto rest = warp(win, icon, side, 0.0);
    QRectF covered, read;
    double area = 0;
    for (const Strip& st : rest) {
      covered = covered.united(st.target);
      read = read.united(st.source);
      area += st.source.width() * st.source.height();
    }
    check(!rest.isEmpty() && near(covered.left(), win.left()) && near(covered.right(), win.right())
              && near(covered.top(), win.top()) && near(covered.bottom(), win.bottom()),
          "at rest the strips cover the window exactly");
    check(near(read.width(), 1) && near(read.height(), 1) && near(area, 1), "at rest the whole snapshot is read once");

    // ── Mid-flight: the near-icon end is narrower than the far end.
    const auto mid = warp(win, icon, side, 0.35);
    const bool vertical = side == Side::DOWN || side == Side::UP;
    const auto across = [vertical](const Strip& st) { return vertical ? st.target.width() : st.target.height(); };
    const auto along = [vertical](const Strip& st) { return vertical ? st.target.center().y() : st.target.center().x(); };
    const Strip& a = mid.first();
    const Strip& b = mid.last();
    const double iconAlong = vertical ? icon.center().y() : icon.center().x();
    const Strip& nearIcon = std::fabs(along(a) - iconAlong) < std::fabs(along(b) - iconAlong) ? a : b;
    const Strip& farIcon = &nearIcon == &a ? b : a;
    check(across(nearIcon) < across(farIcon) * 0.5, "mid-flight the end toward the icon is pinched");

    // ── The bands stay joined, inside the flight's span; at the end the window lies on the icon.
    const auto late = warp(win, icon, side, 0.7);
    bool joined = true;
    for (int k = 0; k + 1 < late.size(); ++k) {
      const QRectF& x = late[k].target;
      const QRectF& y = late[k + 1].target;
      const double gap = vertical ? std::min(std::fabs(x.top() - y.bottom()), std::fabs(x.bottom() - y.top()))
                                  : std::min(std::fabs(x.left() - y.right()), std::fabs(x.right() - y.left()));
      joined = joined && gap < 1e-6;
      check(icon.united(win).adjusted(-1, -1, 1, 1).contains(x), "no band leaves the flight's span");
    }
    check(joined, "no band parts from its neighbour");
    QRectF end;
    for (const Strip& st : warp(win, icon, side, 1.0)) end = end.united(st.target);
    check(near(end.left(), icon.left()) && near(end.right(), icon.right()) && near(end.top(), icon.top())
              && near(end.bottom(), icon.bottom()), "at the end the window lies on the icon");
  }
  // ── The silhouette (the theme swap's Sliding hole): the browser's genieOutline, point for point.
  {
    const QRectF icon(280, 40, 24, 24);
    const auto mid = outline(win, icon, Side::UP, 0.5);
    check(mid.size() == 50, "rows + 1 points down each side");
    const struct { int k; double x, y; } BROWSER[] = {{0, 123.2369, 443.7151}, {12, 190, 201}, {24, 256.7631, 60.655},
                                                     {25, 329.3024, 60.655}, {37, 402, 201}, {49, 474.6976, 443.7151}};
    for (const auto& b : BROWSER)
      check(near(mid[b.k].x(), b.x, 1e-3) && near(mid[b.k].y(), b.y, 1e-3), "matches browser warp.js genieOutline");
    const auto rest = outline(win, icon, Side::UP, 0.0);
    check(near(rest.first().y(), win.bottom()) && near(rest[24].y(), win.top()) && near(rest[24].x(), win.left())
              && near(rest[25].x(), win.right()), "at rest it is the window's own outline");
    bool inIcon = true;
    for (const QPointF& q : outline(win, icon, Side::UP, 1.0)) inIcon = inIcon && icon.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(q);
    check(inIcon, "at the end every point is on the icon");
  }

  return failures ? 1 : 0;
}
