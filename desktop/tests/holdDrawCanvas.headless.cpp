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

static int failures = 0;
static void check(bool ok, const char* msg) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
  if (!ok) ++failures;
}

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
  canvas.selectLineAt(10, 10);  // selects line 0, focuses point 0
  check(canvas.selectedLineIdx() == 0, "line 0 selected by hit-test");
  check(canvas.selectedPoint() == 0, "point 0 focused");
  canvas.deletePoint(canvas.selectedPoint());
  check(canvas.lines().size() == 2 && canvas.lines()[0].points.size() == 2,
        "one point removed from line 0 (2 points remain)");

  std::printf("Alt+Delete path (deleteSelectedLine):\n");
  canvas.selectLineAt(100, 100);  // selects line 1
  check(canvas.selectedLineIdx() == 1, "line 1 selected");
  canvas.deleteSelectedLine();
  check(canvas.lines().size() == 1, "selected line removed (1 remains)");
  check(canvas.selectedLineIdx() == -1, "selection cleared after delete");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
