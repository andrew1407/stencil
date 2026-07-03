// Headless functional check that loads a real image FIXTURE from disk (via QImage) and
// runs it through the same shared code the GUI uses: load into CanvasWidget, take a
// crop, and apply the core image filter to the actual pixels. The desktop counterpart
// of the CLI's decode -> crop -> filter integration test (cli/tests/). Runs offscreen,
// returns non-zero on any failed expectation. Built only when Qt is present.
#include "canvasWidget.hpp"
#include "imageFilter.hpp"
#include "incognitoOverlay.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <QWidget>
#include <cstdio>
#include <cstdlib>

// Count pixels close to `target` (per-channel tolerance) — used to detect the
// accent-violet incognito frame/badge in a grabbed widget render.
static long countNear(const QImage& im, const QColor& target, int tol) {
  long n = 0;
  for (int y = 0; y < im.height(); ++y)
    for (int x = 0; x < im.width(); ++x) {
      const QColor c = im.pixelColor(x, y);
      if (std::abs(c.red() - target.red()) <= tol &&
          std::abs(c.green() - target.green()) <= tol &&
          std::abs(c.blue() - target.blue()) <= tol)
        ++n;
    }
  return n;
}

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

  // 3b) Contour through the real canvas cache path (rebuildFilteredImage →
  //     core::applyContourRGBA on an RGBA8888 copy). A uniform image has no
  //     edges, so every Sobel magnitude is 0 and the output is pure white with
  //     alpha preserved — exactly what the core yields on the same pixels.
  std::printf("contour filter:\n");
  canvas.setImageFilter("contour", QColor("#7c3aed"));
  const QImage contour = canvas.renderToImage(/*withOverlay=*/false);
  check(contour.width() == 8 && contour.height() == 6,
        "contour render keeps the cropped size");
  bool allWhiteOpaque = true;
  for (int y = 0; y < contour.height(); ++y)
    for (int x = 0; x < contour.width(); ++x) {
      const QRgb c = contour.pixel(x, y);
      allWhiteOpaque = allWhiteOpaque && qRed(c) == 255 && qGreen(c) == 255 &&
                       qBlue(c) == 255 && qAlpha(c) == 255;
    }
  check(allWhiteOpaque,
        "contour of a uniform image is all white with alpha preserved");
  // Byte-parity with the core entry point on the same uniform pixels.
  QImage ref(8, 6, QImage::Format_RGBA8888);
  ref.fill(QColor(0x33, 0x66, 0xcc));
  stencil::core::applyContourRGBA(ref.bits(), ref.width(), ref.height());
  check(ref.pixelColor(3, 2) == QColor(Qt::white),
        "core applyContourRGBA agrees (uniform → white)");
  canvas.setImageFilter("none", QColor("#7c3aed"));  // reset for later sections

  // 4) IncognitoOverlay: the viewport-pinned dashed accent frame + badge (port of
  //    the browser's body.incognito-mode outline/badge). It must paint the accent
  //    frame/badge AND be transparent everywhere else, so the canvas shows through —
  //    just like the browser, where the indicator never becomes image content.
  std::printf("incognito overlay:\n");
  const QColor accent = stencil::gui::themePalette(false, "violet").accent;
  const QColor host_bg(0x22, 0x22, 0x22);  // stands in for the dark canvas backdrop
  QWidget host;
  host.resize(320, 200);
  host.setAutoFillBackground(true);
  { QPalette pl; pl.setColor(QPalette::Window, host_bg); host.setPalette(pl); }

  auto* overlay = new IncognitoOverlay(&host);  // child of the "viewport"
  overlay->setTheme(/*dark=*/true, "violet");

  // Off: nothing painted, so a grab of the host is all backdrop, no accent.
  const long offAccent = countNear(host.grab().toImage(), accent, 24);
  check(offAccent == 0, "overlay paints nothing while inactive");

  overlay->setActive(true);
  const QImage shot = host.grab().toImage();  // composites overlay over the host
  const long onAccent = countNear(shot, accent, 24);
  const long bgShown = countNear(shot, host_bg, 16);
  std::printf("  host px: accent off=%ld on=%ld | backdrop-through=%ld\n",
              offAccent, onAccent, bgShown);
  // The solid 3px dashed frame contributes the clean-accent pixels (the badge pill
  // is 90%-alpha-blended, outside the tight tolerance); jumping clear of zero proves
  // the frame paints. Font-independent.
  check(onAccent > 40, "active overlay paints the dashed accent frame + badge");
  // Most of the host area must still read as backdrop — proving the overlay is
  // transparent (the canvas underneath would otherwise be hidden).
  check(bgShown > 320 * 200 / 2,
        "overlay is transparent — the canvas shows through everywhere but the frame/badge");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
