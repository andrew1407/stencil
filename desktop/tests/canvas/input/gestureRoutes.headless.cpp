// Every gesture's move and release, driven through real mouse events on an offscreen CanvasWidget:
// the point, segment (Shift live) and whole-line drags, the pan, the zoom band, the rect sweep and
// the compare divider each do what the browser's onMove / ON_RELEASE entry does, and a move with
// no gesture is a hover. Pins the canvas/input/gestureRoutes table the dispatch reads.
#include "CanvasWidget.hpp"
#include "gestureRoutes.hpp"
#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <cstdio>

using namespace stencil::gui;
using stencil::core::Point;

#include "../../support/check.hpp"

namespace {
  void send(CanvasWidget& c, QEvent::Type type, QPoint at, Qt::MouseButton button,
            Qt::MouseButtons held, Qt::KeyboardModifiers mods) {
    QMouseEvent e(type, QPointF(at), c.mapToGlobal(at), button, held, mods);
    QCoreApplication::sendEvent(&c, &e);
  }

  bool at(const Point& p, double x, double y) { return p.x == x && p.y == y; }

  stencil::core::Line lineOf(std::vector<Point> pts) {
    stencil::core::Line l;
    l.points = std::move(pts);
    return l;
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  CanvasWidget c;
  QImage img(300, 200, QImage::Format_RGB32);
  img.fill(Qt::gray);
  c.loadFromImage(img);
  c.resize(c.imageWidth(), c.imageHeight());
  const auto L = Qt::LeftButton, M = Qt::MiddleButton, N = Qt::NoButton;
  const auto press = QEvent::MouseButtonPress, move = QEvent::MouseMove, up = QEvent::MouseButtonRelease;

  check(!GestureRoutes::of(Gesture::NONE).move && !GestureRoutes::of(Gesture::NONE).release,
        "no gesture routes nowhere");
  for (Gesture g : {Gesture::PAN, Gesture::POINT, Gesture::SEGMENT, Gesture::LINE, Gesture::ZOOM_RECT,
                    Gesture::RECT_DRAW, Gesture::COMPARE_SPLIT})
    check(GestureRoutes::of(g).move && GestureRoutes::of(g).release, "every gesture has a move and a release");

  c.commitLines({lineOf({{50, 50}, {150, 50}, {150, 150}})});
  send(c, press, {50, 50}, L, L, Qt::AltModifier);
  send(c, move, {60, 70}, N, L, Qt::AltModifier);
  send(c, up, {60, 70}, L, N, Qt::AltModifier);
  check(at(c.getLines()[0].points[0], 60, 70) && at(c.getLines()[0].points[1], 150, 50),
        "an Alt-drag of a point moves that point to the cursor");
  c.undo();
  check(at(c.getLines()[0].points[0], 50, 50), "…as one undo step");

  send(c, press, {100, 50}, L, L, Qt::AltModifier);
  send(c, move, {100, 60}, N, L, Qt::AltModifier);
  const auto& pts = c.getLines()[0].points;
  check(at(pts[0], 50, 60) && at(pts[1], 150, 60) && at(pts[2], 150, 150),
        "a segment drag moves the grabbed segment's two endpoints");
  send(c, move, {100, 70}, N, L, Qt::AltModifier | Qt::ShiftModifier);
  check(at(pts[0], 50, 70) && at(pts[1], 150, 70) && at(pts[2], 150, 170), "…Shift moves the whole line");
  send(c, move, {100, 80}, N, L, Qt::AltModifier);
  check(at(pts[0], 50, 80) && at(pts[1], 150, 80) && at(pts[2], 150, 150),
        "…and letting Shift go is measured from the snapshot again");
  send(c, up, {100, 80}, L, N, Qt::AltModifier);
  c.undo();

  send(c, press, {150, 100}, L, L, Qt::AltModifier | Qt::ShiftModifier);
  send(c, move, {160, 100}, N, L, Qt::AltModifier);
  send(c, up, {160, 100}, L, N, Qt::AltModifier);
  check(at(c.getLines()[0].points[0], 60, 50) && at(c.getLines()[0].points[2], 160, 150),
        "an Alt+Shift drag moves the whole line even once Shift lifts");

  QPoint panned;
  int pans = 0;
  QObject::connect(&c, &CanvasWidget::panBy, [&](int dx, int dy, bool) { panned = {dx, dy}; ++pans; });
  send(c, press, {200, 150}, M, M, Qt::NoModifier);
  send(c, move, {205, 157}, N, M, Qt::NoModifier);
  check(pans == 1 && panned == QPoint(5, 7), "a middle drag pans by the pointer's travel");
  send(c, up, {205, 157}, M, N, Qt::NoModifier);
  send(c, move, {230, 170}, N, N, Qt::NoModifier);
  check(pans == 1, "…and the release ends the pan");

  QRectF zoomed;
  int zooms = 0;
  QObject::connect(&c, &CanvasWidget::zoomToRect, [&](const QRectF& r) { zoomed = r; ++zooms; });
  send(c, press, {20, 20}, L, L, Qt::ShiftModifier);
  send(c, move, {120, 90}, N, L, Qt::ShiftModifier);
  send(c, up, {120, 90}, L, N, Qt::ShiftModifier);
  check(zooms == 1 && zoomed == QRectF(20, 20, 100, 70), "a Shift sweep zooms to the swept band");
  send(c, press, {20, 20}, L, L, Qt::ShiftModifier);
  send(c, move, {23, 60}, N, L, Qt::ShiftModifier);
  send(c, up, {23, 60}, L, N, Qt::ShiftModifier);
  check(zooms == 1, "…but not to a band 4 px or narrower");

  const std::size_t before = c.getLines().size();
  c.setDrawMode(CanvasWidget::DrawMode::RECT);
  send(c, press, {30, 120}, L, L, Qt::NoModifier);
  send(c, move, {90, 180}, N, L, Qt::NoModifier);
  send(c, up, {90, 180}, L, N, Qt::NoModifier);
  check(c.getLines().size() == before + 1 && c.getLines().back().locked &&
            at(c.getLines().back().points[0], 30, 120),
        "a rect-tool sweep lands a locked rectangle");
  c.stopDrawingMode();
  c.setDrawMode(CanvasWidget::DrawMode::LINE);

  // The load's default crop decides the width, so the divider starts at half of it.
  const double dragged = 60.0 / c.imageWidth();
  c.setCompareMode(QStringLiteral("vertical"));
  send(c, press, {c.imageWidth() / 2, 100}, L, L, Qt::NoModifier);
  send(c, move, {60, 100}, N, L, Qt::NoModifier);
  check(qAbs(c.getCompareSplit() - dragged) < 1e-9, "a divider drag follows the pointer");
  send(c, up, {60, 100}, L, N, Qt::NoModifier);
  send(c, move, {200, 100}, N, N, Qt::NoModifier);
  check(qAbs(c.getCompareSplit() - dragged) < 1e-9, "…and the release lets it go");
  c.setCompareMode(QStringLiteral("none"));

  int hovers = 0;
  QObject::connect(&c, &CanvasWidget::hovered, [&](double, double) { ++hovers; });
  send(c, move, {10, 10}, N, N, Qt::NoModifier);
  check(hovers == 1, "a move with no gesture is a hover");

  std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
