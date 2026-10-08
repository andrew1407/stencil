// A hidden kind of mark is out of reach of every pointer path, driven through real events on an
// offscreen CanvasWidget: click-select, Ctrl+click, the close click, hover, double-click delete,
// Alt+wheel, the Alt drags, the pull-out and a hold. Drawing a new point keeps working.
// Browser twin: browser/tests/core/hiddenMarks.test.js.
#include "CanvasWidget.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cstdio>

using namespace stencil::gui;
using stencil::core::Point;

#include "../../support/check.hpp"

namespace {
  void send(CanvasWidget& c, QEvent::Type type, QPoint at, Qt::MouseButton button, Qt::MouseButtons held,
            Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QMouseEvent e(type, QPointF(at), c.mapToGlobal(at), button, held, mods);
    QCoreApplication::sendEvent(&c, &e);
  }

  void click(CanvasWidget& c, QPoint at, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    send(c, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, mods);
    send(c, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, mods);
  }

  void drag(CanvasWidget& c, QPoint from, QPoint to, Qt::KeyboardModifiers mods) {
    send(c, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, mods);
    send(c, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
    send(c, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
  }

  void wheel(CanvasWidget& c, QPoint at) {
    QWheelEvent e(QPointF(at), QPointF(c.mapToGlobal(at)), QPoint(), QPoint(0, 120), Qt::NoButton,
                  Qt::AltModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&c, &e);
  }

  const std::vector<Point> CORNER{{100, 100}, {300, 100}, {300, 250}};

  bool untouched(const CanvasWidget& c) {
    if (c.getLines().size() != 1 || c.getLines()[0].points.size() != CORNER.size()) return false;
    const auto& pts = c.getLines()[0].points;
    for (std::size_t i = 0; i < CORNER.size(); ++i)
      if (pts[i].x != CORNER[i].x || pts[i].y != CORNER[i].y) return false;
    return true;
  }

  // A real double-click's last two events: one that misses falls through to a press, so its release follows.
  void doubleClick(CanvasWidget& c, QPoint at) {
    send(c, QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton);
    send(c, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  CanvasWidget c;
  QImage img(400, 300, QImage::Format_RGB32);
  img.fill(Qt::gray);
  c.loadFromImage(img);
  c.resize(c.imageWidth(), c.imageHeight());
  const QPoint vertex(300, 100), body(200, 103);
  // One open corner line, a fresh selection, and which kinds are drawn.
  const auto show = [&c](bool points, bool lines) {
    if (c.getIsDrawing()) c.stopDrawingMode();
    c.setShowPoints(points);
    c.setShowLines(lines);
    stencil::core::Line corner;
    corner.points = CORNER;
    corner.thickness = 3;
    c.setLines({corner});
    c.deselect();
  };

  std::printf("click-select and Ctrl+click:\n");
  show(true, true);
  click(c, vertex);
  check(c.getSelectedLineIdx() == 0 && c.getSelectedPoint() == 1, "shown: the vertex is focused on its line");
  show(false, true);
  click(c, vertex);
  check(c.getSelectedLineIdx() == 0 && c.getSelectedPoint() == -1, "points hidden: the line, no focused point");
  show(true, false);
  click(c, body);
  check(c.getSelectedLineIdx() == -1, "lines hidden: a click on the stroke selects nothing");
  click(c, {299, 101});
  check(c.getSelectedLineIdx() == 0 && c.getSelectedPoint() == 1, "…its shown points still do");
  show(false, false);
  click(c, vertex);
  check(c.getSelectedLineIdx() == -1, "both hidden: nothing");
  show(true, false);
  click(c, body, Qt::ControlModifier);
  check(c.getLines().size() == 2 && c.getLines()[0].points.size() == 3,
        "Ctrl+click on a hidden stroke inserts nothing into it: a point of its own instead");
  show(true, true);
  click(c, body, Qt::ControlModifier);
  check(c.getLines().size() == 1 && c.getLines()[0].points.size() == 4, "a shown stroke takes the point");

  std::printf("the close click:\n");
  for (const bool points : {true, false}) {
    show(points, true);
    c.startDrawingMode();
    for (const QPoint p : {QPoint(20, 20), QPoint(80, 20), QPoint(80, 80), QPoint(21, 21)}) click(c, p);
    if (points)
      check(!c.getIsDrawing() && c.getLines().size() == 2 && c.getLines()[1].locked,
            "shown, the first point closes the shape");
    else
      check(c.getIsDrawing() && c.getCurrentLine().points.size() == 4 && c.getLines().size() == 1,
            "points hidden, nothing closes: the click draws its point");
  }

  std::printf("hover:\n");
  int hoverPt = -2, overLine = -2;
  QObject::connect(&c, &CanvasWidget::canvasHoverChanged, [&](int, int pi, int over) {
    hoverPt = pi;
    overLine = over;
  });
  const auto hoverAt = [&c](QPoint at) {
    send(c, QEvent::MouseMove, {390, 290}, Qt::NoButton, Qt::NoButton);   // off every mark first
    send(c, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
  };
  const auto cursor = [&c] { return c.cursor().shape(); };
  show(true, true);
  hoverAt(vertex);
  check(hoverPt == 1 && overLine == 0 && cursor() == Qt::PointingHandCursor, "shown: the ring, the tint, the hand");
  show(false, true);
  hoverAt(vertex);
  check(hoverPt == -1 && overLine == 0, "points hidden: no ring; the stroke is still under the pointer");
  show(true, false);
  hoverAt(body);
  check(overLine == -1 && cursor() == Qt::CrossCursor, "lines hidden: the stroke is empty canvas");
  show(false, false);
  hoverAt(vertex);
  check(hoverPt == -1 && overLine == -1 && cursor() == Qt::CrossCursor, "both hidden: nothing");

  std::printf("double-click and Alt+wheel:\n");
  show(true, false);
  doubleClick(c, body);
  check(untouched(c), "a double-click on a hidden stroke deletes nothing");
  show(true, true);
  doubleClick(c, body);
  check(c.getLines().empty(), "on a shown one it deletes the line");
  show(true, false);
  wheel(c, body);
  check(c.getLines()[0].thickness == 3, "Alt+wheel over a hidden stroke changes nothing");
  show(false, false);
  wheel(c, vertex);
  check(c.getLines()[0].thickness == 3, "…nor over a hidden point");
  show(false, true);
  wheel(c, body);
  check(c.getLines()[0].thickness == 4, "…over a shown stroke it thickens it");

  std::printf("the Alt drags and the pull-out:\n");
  show(true, true);
  drag(c, vertex, {300, 120}, Qt::AltModifier);
  check(c.getLines()[0].points[1].y == 120 && c.getLines()[0].points[0].y == 100, "shown: Alt drags the vertex");
  show(false, true);
  drag(c, vertex, {300, 120}, Qt::AltModifier);
  check(c.getLines()[0].points[0].y == 120 && c.getLines()[0].points[1].y == 120,
        "points hidden: the stroke under it moves instead");
  int pans = 0;
  QObject::connect(&c, &CanvasWidget::panBy, [&pans](int, int, bool) { ++pans; });
  show(false, false);
  drag(c, vertex, {300, 120}, Qt::AltModifier);
  check(untouched(c) && pans == 1, "both hidden: an Alt drag pans");
  drag(c, vertex, {300, 120}, Qt::AltModifier | Qt::ShiftModifier);
  check(untouched(c), "…an Alt+Shift drag moves no line");
  drag(c, body, {200, 120}, Qt::AltModifier | Qt::ControlModifier);
  check(untouched(c), "…and the pull-out finds nothing to pull");

  std::printf("hold-to-draw:\n");
  c.setHoldDrawDelay(100);
  const auto holdAt = [&c](QPoint at) {
    send(c, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 320) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  };
  show(true, false);
  holdAt(body);
  check(c.getIsDrawing() && c.getCurrentLine().points.size() == 1 && c.getLines()[0].points.size() == 3,
        "a hold over a hidden stroke starts a fresh one");
  send(c, QEvent::MouseButtonRelease, body, Qt::LeftButton, Qt::NoButton);
  show(true, true);
  holdAt(body);
  check(c.getIsDrawing() && c.getLines()[0].points.size() == 4, "over a shown one it inserts and continues");
  send(c, QEvent::MouseButtonRelease, body, Qt::LeftButton, Qt::NoButton);

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
