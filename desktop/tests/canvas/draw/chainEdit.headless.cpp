// Closing a shape, and the two ways back out of one (src/canvas/draw/chainEdit.hpp) — the
// desktop port of browser/js/core/touch/dragGestures.js, carrying that suite's cases, plus the
// wiring on the real widget: Alt+Ctrl+drag pulls a new point out of the line under the
// cursor and breaks a closed area open at that spot. Runs offscreen.
#include "CanvasWidget.hpp"
#include "chainEdit.hpp"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <cmath>
#include <cstdio>

#include "../../support/check.hpp"

using stencil::gui::CanvasWidget;
namespace chain = stencil::gui::chain;
namespace core = stencil::core;

typedef std::vector<core::Point> Pts;

static bool same(const core::Point& a, double x, double y) {
  return std::fabs(a.x - x) < 1e-9 && std::fabs(a.y - y) < 1e-9;
}
static bool same(const Pts& got, const Pts& want) {
  if (got.size() != want.size()) return false;
  for (std::size_t i = 0; i < got.size(); ++i)
    if (!same(got[i], want[i].x, want[i].y)) return false;
  return true;
}

// A shape closed by clicking its first point: the closing DUPLICATE at the end.
static core::Line closedShape() {
  core::Line l;
  l.points = {{0, 0}, {10, 0}, {10, 10}, {0, 0}};
  l.locked = true;
  return l;
}
// The same shape drawn roomy: no two vertices within one hit radius, or every grab answers point 0
// (core findNearestPoint) and the seam lands there whatever the user pointed at.
static core::Line roomyShape() {
  core::Line l;
  l.points = {{0, 0}, {60, 0}, {60, 60}, {0, 0}};
  l.locked = true;
  return l;
}
// A rect: locked, four corners, no duplicate.
static core::Line rect() {
  core::Line l;
  l.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  l.locked = true;
  return l;
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM

  // ── The ring ──────────────────────────────────────────────────────────────
  check(chain::ringPoints(closedShape().points).size() == 3, "the closing duplicate is dropped");
  check(chain::ringPoints(rect().points).size() == 4, "a rect closes without a duplicate");
  check(chain::ringPoints(Pts{{1, 2}, {3, 4}}).size() == 2, "an open line is its own ring");
  check(chain::ringPoints(Pts{}).empty(), "and an empty one does not throw");

  check(same(chain::openRingAt(closedShape().points, 1), Pts{{10, 0}, {10, 10}, {0, 0}, {10, 0}}),
        "opening at vertex 1 re-roots the ring and ends on a copy of it");
  check(same(chain::openRingAt(closedShape().points, 0), Pts{{0, 0}, {10, 0}, {10, 10}, {0, 0}}),
        "breaking at point 0 is the shape it already looked like — now open");
  check(same(chain::openRingAt(rect().points, 3), Pts{{0, 10}, {0, 0}, {10, 0}, {10, 10}, {0, 10}}),
        "a rect opens the same way");

  // ── Unchaining ────────────────────────────────────────────────────────────
  {
    core::Line shape = closedShape();
    shape.fillColor = "#3399ff";
    check(chain::unchainLine(shape), "an area unchains");
    check(!shape.locked && same(shape.points, Pts{{0, 0}, {10, 0}, {10, 10}}),
          "the closing duplicate is gone and it is no longer an area");
    // An open line has no area to paint, so the colour goes with the shape: "transparent"
    // is the app's own "no fill", so the Fill field comes up CLEARED if it is closed again.
    check(shape.fillColor == "transparent", "unchaining clears the fill");

    core::Line r = rect();
    check(chain::unchainLine(r) && same(r.points, Pts{{0, 0}, {10, 0}, {10, 10}, {0, 10}}),
          "a rect keeps all four corners");
    core::Line open;
    open.points = {{0, 0}, {1, 1}};
    check(!chain::unchainLine(open), "an open line has nothing to unchain");
  }

  // ── Pulling a new point out ───────────────────────────────────────────────
  {
    core::Line line;
    line.points = {{0, 0}, {10, 0}, {20, 0}};
    check(chain::pullOutPoint(line, {true, 1}, 11, 4) == 2, "the copy sits after the point it came from");
    check(same(line.points, Pts{{0, 0}, {10, 0}, {10, 0}, {20, 0}}), "…as a duplicate in place");

    core::Line seg;
    seg.points = {{0, 0}, {20, 0}};
    check(chain::pullOutPoint(seg, {false, 1}, 9, 5) == 1, "a segment grab inserts between its ends");
    check(same(seg.points, Pts{{0, 0}, {9, 5}, {20, 0}}), "…right under the cursor");

    core::Line shape = closedShape();
    shape.fillColor = "#3399ff";
    const int idx = chain::pullOutPoint(shape, {true, 1}, 11, 4);
    check(!shape.locked, "pulling out of an area stops it being one");
    check(shape.fillColor == "transparent", "…and its fill goes with the shape");
    check(idx == static_cast<int>(shape.points.size()) - 1, "the free end is what you drag");
    check(same(shape.points, Pts{{10, 0}, {10, 10}, {0, 0}, {10, 0}}),
          "and the seam is at the vertex pulled, not at point 0");

    core::Line r = rect();
    const int ridx = chain::pullOutPoint(r, {false, 2}, 14, 6);
    check(!r.locked && same(r.points[static_cast<std::size_t>(ridx)], 14, 6),
          "a segment grab on an area breaks it and the loose end follows the pointer");

    core::Line empty;
    check(chain::pullOutPoint(empty, {true, 0}, 0, 0) == -1, "nothing to pull out of");
    check(chain::pullOutPoint(line, {true, -1}, 0, 0) == -1, "…nor from no target");
  }

  // ── The gesture, on the real widget ───────────────────────────────────────
  {
    CanvasWidget canvas;
    QImage img(200, 200, QImage::Format_RGB32);
    img.fill(Qt::white);
    canvas.loadFromImage(img);
    canvas.setScale(1.0);
    canvas.resize(canvas.imageWidth(), canvas.imageHeight());
    canvas.setLines({roomyShape()});
    canvas.selectLineByIndex(0);

    // Alt+Ctrl-press on vertex 1 (60,0): the area opens there and the new free end is
    // what the drag moves.
    const QPoint at(60, 0);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), canvas.mapToGlobal(at),
                      Qt::LeftButton, Qt::LeftButton, Qt::AltModifier | Qt::ControlModifier);
    QCoreApplication::sendEvent(&canvas, &press);
    check(!canvas.getLines()[0].locked, "Alt+Ctrl+press broke the area open");
    check(canvas.getLines()[0].points.size() == 4, "three ring points plus the free end");
    check(same(canvas.getLines()[0].points, Pts{{60, 0}, {60, 60}, {0, 0}, {60, 0}}),
          "…seamed at the vertex grabbed");
    check(canvas.getSelectedPoint() == 3, "the free end is focused for the drag");

    // Dragging really moves that end, and only it.
    const QPoint to(100, 80);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), canvas.mapToGlobal(to), Qt::NoButton,
                     Qt::LeftButton, Qt::AltModifier | Qt::ControlModifier);
    QCoreApplication::sendEvent(&canvas, &move);
    check(same(canvas.getLines()[0].points[3], 100, 80), "the pulled end followed the cursor");
    check(same(canvas.getLines()[0].points[0], 60, 0), "and the vertex it came from stayed put");
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(to), canvas.mapToGlobal(to),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &rel);

    // macOS delivers Ctrl+Left as a RIGHT button press, so the same chord must pull out
    // there too — and must NOT ask for a context menu over the point being dragged.
    canvas.setLines({roomyShape()});
    canvas.selectLineByIndex(0);
    int contextMenus = 0;
    QObject::connect(&canvas, &CanvasWidget::contextRequested, &canvas,
                     [&contextMenus](const QPoint&) { ++contextMenus; });
    const QPoint vertex(60, 60);
    QMouseEvent rightPress(QEvent::MouseButtonPress, QPointF(vertex), canvas.mapToGlobal(vertex),
                           Qt::RightButton, Qt::RightButton,
                           Qt::AltModifier | Qt::ControlModifier);
    QCoreApplication::sendEvent(&canvas, &rightPress);
    check(contextMenus == 0, "the pull-out chord asked for no context menu");
    check(!canvas.getLines()[0].locked, "…and it pulled out, as the left-button press does");

    // A plain right-click still opens the menu.
    QMouseEvent plainRight(QEvent::MouseButtonPress, QPointF(vertex), canvas.mapToGlobal(vertex),
                           Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &plainRight);
    check(contextMenus == 1, "a plain right-click still opens the context menu");

    // …and the button route puts a rect back to an open line.
    canvas.setLines({rect()});
    canvas.selectLineByIndex(0);
    canvas.unchainSelectedLine();
    check(!canvas.getLines()[0].locked && canvas.getLines()[0].points.size() == 4,
          "unchainSelectedLine opens a rect");
    canvas.unchainSelectedLine();
    check(canvas.getLines()[0].points.size() == 4, "…and asking again changes nothing");
  }

  // The counter every check() feeds — without this the suite passed with failures in it.
  std::printf("chainEdit: %s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
