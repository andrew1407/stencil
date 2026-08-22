// Headless functional check of the desktop hold-to-draw public surface +
// selection-delete paths (the ones the Alt+Delete / Alt+Shift+Delete shortcuts
// invoke). Runs offscreen so it needs no display. The hold/dwell *timing* state
// machine is covered exhaustively by core/tests/holdDraw.test.cpp; here we just
// confirm the CanvasWidget wiring (delay clamp + delete API) on the real widget.
// Returns non-zero on any failed expectation.
#include "canvasWidget.hpp"
#include <QApplication>
#include <QImage>
#include <cstdio>

using namespace stencil::gui;

#include "support/check.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);  // offscreen via QT_QPA_PLATFORM

  CanvasWidget canvas;
  QImage img(300, 200, QImage::Format_RGB32);
  img.fill(Qt::gray);
  canvas.loadFromImage(img);

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
