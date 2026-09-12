// Headless functional check of the desktop hold-to-draw public surface +
// selection-delete paths (the ones the Alt+Delete / Alt+Shift+Delete shortcuts
// invoke). Runs offscreen so it needs no display. The hold/dwell *timing* state
// machine is covered exhaustively by core/tests/holdDraw.test.cpp; here we just
// confirm the CanvasWidget wiring (delay clamp + delete API) on the real widget.
// Returns non-zero on any failed expectation.
#include "canvasWidget.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <cstdio>

using namespace stencil::gui;

#include "support/check.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);  // offscreen via QT_QPA_PLATFORM

  CanvasWidget canvas;
  QImage img(300, 200, QImage::Format_RGB32);
  img.fill(Qt::gray);
  canvas.loadFromImage(img);

  // ── The rect tool never seeds a freehand line ─────────────────────────────
  // A press-and-hold with drawing OFF auto-enters drawing and starts a stroke. With the
  // RECT tool selected that drew a LINE — the tool you picked making the wrong shape.
  {
    canvas.setHoldDrawDelay(100);
    canvas.resize(canvas.imageWidth(), canvas.imageHeight());
    const QPoint at(120, 90);
    const auto send = [&](QEvent::Type type, Qt::MouseButton held) {
      QMouseEvent e(type, QPointF(at), canvas.mapToGlobal(at), Qt::LeftButton, held,
                    Qt::NoModifier);
      QCoreApplication::sendEvent(&canvas, &e);
    };
    const auto hold = [&] {
      send(QEvent::MouseButtonPress, Qt::LeftButton);
      QElapsedTimer t;
      t.start();
      while (t.elapsed() < 320) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    const auto release = [&] { send(QEvent::MouseButtonRelease, Qt::NoButton); };

    canvas.setDrawMode(CanvasWidget::DrawMode::RECT);
    hold();
    // The rect tool never seeds a freehand LINE — that is the invariant. It does enter
    // drawing mode now, because its own press starts the rubber band (see below).
    check(canvas.currentLine().points.empty(), "holding with the rect tool starts no line");
    release();

    canvas.setDrawMode(CanvasWidget::DrawMode::LINE);
    hold();
    check(canvas.isDrawing() && canvas.currentLine().points.size() == 1,
          "…while the line tool still seeds its stroke on the hold");
    release();
    canvas.stopDrawingMode();
    canvas.clearAll();

    // …and the rect tool is not left inert by that: its PRESS starts the sweep, turning
    // drawing on by itself. Before, it did nothing at all until Draw was pressed too.
    canvas.setDrawMode(CanvasWidget::DrawMode::RECT);
    check(!canvas.isDrawing(), "starting from not-drawing");
    const QPoint from(30, 30), to(120, 90);
    QMouseEvent down(QEvent::MouseButtonPress, QPointF(from), canvas.mapToGlobal(from),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &down);
    check(canvas.isDrawing(), "the rect press turned drawing on by itself");
    QMouseEvent sweep(QEvent::MouseMove, QPointF(to), canvas.mapToGlobal(to), Qt::NoButton,
                      Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &sweep);
    QMouseEvent up(QEvent::MouseButtonRelease, QPointF(to), canvas.mapToGlobal(to),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &up);
    check(canvas.lines().size() == 1, "…and the sweep really made a rect");
    check(canvas.lines().size() == 1 && canvas.lines()[0].points.size() == 4 &&
              canvas.lines()[0].locked,
          "four corners, locked as an area");
    canvas.setDrawMode(CanvasWidget::DrawMode::LINE);
    canvas.stopDrawingMode();
    canvas.clearAll();
    canvas.setHoldDrawDelay(500);
  }

  // ── Resting the last point on the first CLOSES the shape ──────────────────
  // REGRESSION: a triangle drawn by hold-to-draw ended with a point dropped on top of the
  // first one and stayed an open line that merely looked closed. The close check lived
  // only in the click path; both routes now go through tryCloseShapeAt.
  {
    canvas.clearAll();
    canvas.setHoldDrawDelay(100);
    canvas.resize(canvas.imageWidth(), canvas.imageHeight());
    const auto press = [&](const QPoint& at) {
      QMouseEvent e(QEvent::MouseButtonPress, QPointF(at), canvas.mapToGlobal(at),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(&canvas, &e);
    };
    const auto restAt = [&](const QPoint& at) {
      QMouseEvent e(QEvent::MouseMove, QPointF(at), canvas.mapToGlobal(at), Qt::NoButton,
                    Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(&canvas, &e);
      QElapsedTimer t;
      t.start();
      while (t.elapsed() < 320) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    };
    const QPoint first(40, 40);
    press(first);
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 320) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    check(canvas.isDrawing() && canvas.currentLine().points.size() == 1, "the hold seeded a stroke");
    restAt(QPoint(200, 60));
    restAt(QPoint(120, 160));
    check(canvas.currentLine().points.size() == 3, "three corners dropped");
    restAt(first);                       // …and back onto the first point
    check(!canvas.isDrawing(), "closing the shape ended the stroke");
    // Guarded: check() reports and CARRIES ON, so indexing an empty list on the failure
    // path would crash the run instead of naming what broke.
    const bool committed = canvas.lines().size() == 1;
    check(committed, "the closed shape was committed");
    check(committed && canvas.lines()[0].locked,
          "…as a LOCKED area, not an open line that looks shut");
    check(committed && canvas.lines()[0].points.size() == 4,
          "three corners plus the closing point");
    check(committed && (canvas.lines()[0].fillColor == "transparent" ||
                        canvas.lines()[0].fillColor.empty()),
          "…fillable, and transparent until a colour is picked");
    // …and NOTHING is selected: finishing a shape ends like finishing an ordinary line,
    // with no selected-line bar popping up over the picture just drawn.
    check(canvas.selectedLineIdx() == -1, "closing a shape selects nothing");
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(first), canvas.mapToGlobal(first),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &rel);
    canvas.clearAll();
    canvas.setHoldDrawDelay(500);
  }

  // ── The grab circle is the same size on SCREEN at any zoom ────────────────
  // The other half of the same report: the close slack was fixed in IMAGE pixels, so at
  // 25% zoom the first point was a ~3px target that an ordinary click missed.
  {
    canvas.deselect();
    canvas.setScale(0.25);            // widget px = image px / 4
    canvas.startDrawingMode();
    const auto clickAt = [&](const QPoint& at) {
      QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), canvas.mapToGlobal(at),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(&canvas, &press);
      QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(at), canvas.mapToGlobal(at),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(&canvas, &rel);
    };
    clickAt(QPoint(10, 10));          // image (40, 40)
    clickAt(QPoint(50, 15));          // image (200, 60)
    clickAt(QPoint(30, 40));          // image (120, 160)
    check(canvas.currentLine().points.size() == 3, "three points down at 25% zoom");
    // Six screen pixels off the first dot — an ordinary "on the dot" click, and 27 IMAGE
    // pixels away: far outside the old fixed 12-image-pixel grab.
    clickAt(QPoint(16, 13));
    check(!canvas.isDrawing(), "the click closed the shape");
    check(canvas.lines().size() == 1 && canvas.lines()[0].locked,
          "a normal click on the dot closes the shape when zoomed out");   // short-circuits
    canvas.setScale(1.0);
    canvas.clearAll();
  }

  std::printf("hold-to-draw delay clamp:\n");
  canvas.setHoldDrawDelay(750);
  check(canvas.holdDrawDelay() == 750, "set within range is kept (750)");
  canvas.setHoldDrawDelay(10);
  check(canvas.holdDrawDelay() == 100, "below-min clamps to 100");
  canvas.setHoldDrawDelay(99999);
  check(canvas.holdDrawDelay() == 3000, "above-max clamps to 3000");
  canvas.setHoldDrawDelay(500);

  // Seed two lines so the selection-delete paths have something to act on.
  stencil::core::Line a;
  a.points = {{10, 10}, {40, 10}, {40, 40}};
  stencil::core::Line b;
  b.points = {{100, 100}, {150, 120}};
  canvas.setLines({a, b});
  check(canvas.lines().size() == 2, "two lines seeded");

  std::printf("Alt+Shift+Delete path (deletePoint of the focused point):\n");
  canvas.selectLineAt(10, 10);
  check(canvas.selectedLineIdx() == 0, "line 0 selected by hit-test");
  check(canvas.selectedPoint() == 0, "point 0 focused");
  canvas.deletePoint(canvas.selectedPoint());
  check(canvas.lines().size() == 2 && canvas.lines()[0].points.size() == 2,
        "one point removed from line 0 (2 points remain)");

  std::printf("Alt+Delete path (deleteSelectedLine):\n");
  canvas.selectLineAt(100, 100);
  check(canvas.selectedLineIdx() == 1, "line 1 selected");
  canvas.deleteSelectedLine();
  check(canvas.lines().size() == 1, "selected line removed (1 remains)");
  check(canvas.selectedLineIdx() == -1, "selection cleared after delete");

  std::printf("zoom-aware hit thresholds (screen-constant radius, browser parity):\n");
  stencil::core::Line far;
  far.points = {{100, 100}, {200, 100}};
  canvas.setLines({far});
  canvas.setScale(0.25);   // zoomed out: 8 screen px = 32 image px
  check(canvas.selectLineAt(150, 120) == 0, "zoomed out, a 20-image-px miss still selects");
  canvas.deselect();
  canvas.setScale(4.0);    // zoomed in: 8 screen px = 2 image px
  check(canvas.selectLineAt(150, 113) == -1, "zoomed in, a 13-image-px miss does NOT grab");
  canvas.setScale(1.0);

  std::printf("deletePoint erases the line it shows, not lines_.back():\n");
  stencil::core::Line solo;
  solo.points = {{20, 20}};
  stencil::core::Line other;
  other.points = {{200, 150}, {250, 150}};
  canvas.setLines({solo, other});
  canvas.selectLineAt(20, 20);   // selects line 0 (the single-point line)
  check(canvas.selectedLineIdx() == 0 && canvas.selectedPoint() == 0,
        "line 0 / point 0 selected");
  canvas.deletePoint(0);         // empties line 0 — it must be the one erased
  check(canvas.lines().size() == 1, "one line remains");
  check(!canvas.lines().empty() && canvas.lines()[0].points.size() == 2,
        "the SURVIVING line is the two-point one (pop_back removed the wrong line)");

  std::printf("point colour resolves at draw time (line recolours leave points alone):\n");
  canvas.deselect();
  canvas.setDefaults("#ff0000", 2.0, 4.0, "solid", "");   // empty = follow line colour
  canvas.startDrawingMode();
  check(canvas.currentLine().pointColor == "#ff0000",
        "empty point-colour setting resolves to the line colour at draw time");
  canvas.stopDrawingMode();
  canvas.setDefaults("#ff0000", 2.0, 4.0, "solid", "#00ff00");
  canvas.startDrawingMode();
  check(canvas.currentLine().pointColor == "#00ff00", "explicit setting is kept");
  canvas.stopDrawingMode();

  stencil::core::Line legacy;   // pre-pointColor layout: '' = inherit
  legacy.points = {{40, 40}, {90, 40}};
  legacy.color = "#ff0000";
  legacy.pointColor = "";
  canvas.setLines({legacy});
  canvas.selectLineAt(40, 40);
  canvas.setSelectedLineColor("#0000ff");
  check(canvas.lines()[0].color == "#0000ff", "stroke recoloured");
  check(canvas.lines()[0].pointColor == "#ff0000",
        "points keep drawing in their pre-recolour colour");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
