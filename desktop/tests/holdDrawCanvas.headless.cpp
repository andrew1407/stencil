// Headless functional check of the desktop hold-to-draw public surface and the selection-delete paths
// the Alt+Delete / Alt+Shift+Delete shortcuts invoke, run offscreen. The hold/dwell timing state
// machine is covered by core/tests/holdDraw.test.cpp; here it is the CanvasWidget wiring (delay clamp
// + delete API) on the real widget. Returns non-zero on any failed expectation.
#include "CanvasWidget.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <cstdio>

using namespace stencil::gui;

#include "support/check.hpp"
void deleteAndColourPaths(CanvasWidget& canvas);

int main(int argc, char** argv) {
  QApplication app(argc, argv);  // offscreen via QT_QPA_PLATFORM

  CanvasWidget canvas;
  QImage img(300, 200, QImage::Format_RGB32);
  img.fill(Qt::gray);
  canvas.loadFromImage(img);

  // The rect tool never seeds a freehand line: a press-and-hold with drawing OFF auto-enters drawing
  // and starts a stroke, and with the RECT tool selected that must not be a LINE.
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

  // Resting the last point on the first CLOSES the shape: the close check lived only in the click path,
  // so both routes now go through tryCloseShapeAt.
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

  // The grab circle is the same size on SCREEN at any zoom: a close slack fixed in IMAGE pixels left
  // the first point a ~3px target at 25% zoom.
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
  deleteAndColourPaths(canvas);

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
