// Headless functional check that loads a real image FIXTURE from disk (via QImage) and
// runs it through the same shared code the GUI uses: load into CanvasWidget, take a
// crop, and apply the core image filter to the actual pixels. The desktop counterpart
// of the CLI's decode -> crop -> filter integration test (cli/tests/). Runs offscreen,
// returns non-zero on any failed expectation. Built only when Qt is present.
#include "canvasWidget.hpp"
#include "imageFilter.hpp"

#include <QApplication>
#include <QImage>
#include <cstdio>

using namespace stencil::gui;
using stencil::core::CropRect;

static int failures = 0;
static void check(bool ok, const char* msg) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
  if (!ok) ++failures;
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);  // offscreen via QT_QPA_PLATFORM

  // 1) Load the committed fixture from disk (16x12 solid #3366cc).
  const QString path = QStringLiteral(STENCIL_FIXTURES_DIR "/sample.png");
  QImage img;
  std::printf("load fixture %s:\n", qPrintable(path));
  check(img.load(path), "fixture PNG loaded");
  check(img.width() == 16 && img.height() == 12, "fixture is 16x12");
  const QColor px = img.pixelColor(0, 0);
  check(px.red() == 0x33 && px.green() == 0x66 && px.blue() == 0xcc,
        "fixture pixel is #3366cc");

  // 2) Drive it through CanvasWidget (the real load + default crop path).
  CanvasWidget canvas;
  canvas.setPageCm(21.0, 29.7);  // A4 (portrait)
  canvas.loadFromImage(img);
  check(canvas.originalImage().width() == 16 && canvas.originalImage().height() == 12,
        "original image kept at 16x12");
  const CropRect c0 = canvas.cropRect();
  check(c0.width > 0 && c0.height > 0 && c0.width <= 16 && c0.height <= 12,
        "default crop is a non-empty sub-rect of the image");

  // Take an explicit 8x6 crop from the top-left and confirm it sticks.
  canvas.applyCrop(CropRect{0, 0, 8, 6}, /*recalc=*/true);
  check(canvas.imageWidth() == 8 && canvas.imageHeight() == 6,
        "working image is the 8x6 crop");

  // 3) Apply the core image filter (bw) to the real pixels and confirm it greyscales.
  QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
  stencil::core::applyFilterRGBA(stencil::core::FilterMode::Bw, rgba.bits(),
                                 static_cast<std::size_t>(rgba.width()) * rgba.height(),
                                 0, 0, 0);
  const QRgb after = rgba.pixel(0, 0);  // RGBA8888 read back as ARGB QRgb
  std::printf("bw filter pixel: r=%d g=%d b=%d a=%d\n", qRed(after), qGreen(after),
              qBlue(after), qAlpha(after));
  check(qRed(after) == qGreen(after) && qGreen(after) == qBlue(after),
        "bw filter greyscaled the pixel (r==g==b)");
  check(qAlpha(after) == 255, "bw filter preserved alpha");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
