// Headless functional check of the desktop crop integration (CanvasWidget +
// cropGeometry), run offscreen so it needs no display. Exercises the SAME built
// code the GUI uses: default centered crop on load, crop-local point rescale on
// a same-orientation resize, and line clearing on an orientation flip. Returns
// non-zero on any failed expectation. Not part of stencil_tests (that target is
// Qt-free); built only when Qt is present (see CMakeLists).
#include "canvasWidget.hpp"
#include <QApplication>
#include <QImage>
#include <cmath>
#include <cstdio>

using namespace stencil::gui;
using stencil::core::CropRect;

static int failures = 0;
static void check(bool ok, const char* msg) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
  if (!ok) ++failures;
}
static bool near(double a, double b, double eps = 0.5) { return std::fabs(a - b) <= eps; }

int main(int argc, char** argv) {
  QApplication app(argc, argv);  // offscreen via QT_QPA_PLATFORM

  CanvasWidget canvas;
  canvas.setPageCm(29.7, 42.0);  // A3 (√2)

  // A 400x200 (album) image: default crop = A3 album aspect, sides cut, centered.
  QImage img(400, 200, QImage::Format_RGB32);
  img.fill(Qt::gray);
  canvas.loadFromImage(img);

  std::printf("default crop of 400x200 @ A3:\n");
  const CropRect c0 = canvas.cropRect();
  check(canvas.imageWidth() == 283, "working width == 283 (sides cut to album √2)");
  check(canvas.imageHeight() == 200, "working height == 200 (full height kept)");
  check(near(c0.width, 283) && near(c0.height, 200), "cropRect is 283x200");
  check(near(c0.x, 58.5, 1.0) && near(c0.y, 0), "crop centered horizontally (x≈58)");

  // Place a line, then resize the crop to full width (same album orientation):
  // points must rescale by newWidth/oldWidth, lines preserved.
  stencil::core::Line line;
  line.points = {{10.0, 10.0}, {20.0, 20.0}};
  canvas.setLines({line});
  const double oldW = canvas.cropRect().width;
  const double newH = std::round(400.0 * 200.0 / 283.0);  // keep album aspect
  canvas.applyCrop(CropRect{0, 0, 400, newH}, /*recalc=*/true);
  const double scale = canvas.cropRect().width / oldW;
  std::printf("resize within album (scale=%.4f):\n", scale);
  check(canvas.lines().size() == 1, "line preserved on same-orientation resize");
  check(canvas.lines().size() == 1 &&
            near(canvas.lines()[0].points[0].x, 10.0 * scale) &&
            near(canvas.lines()[0].points[0].y, 10.0 * scale),
        "point rescaled by the width ratio (page relation preserved)");

  // Flip to portrait (height > width): orientation changed -> lines cleared.
  canvas.applyCrop(CropRect{0, 0, 120, 200}, /*recalc=*/true);
  std::printf("flip to portrait:\n");
  check(canvas.lines().empty(), "lines removed on orientation change");
  check(canvas.imageWidth() == 120 && canvas.imageHeight() == 200,
        "working image is the portrait crop (120x200)");
  check(canvas.originalImage().width() == 400 && canvas.originalImage().height() == 200,
        "original image kept untouched at 400x200");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
