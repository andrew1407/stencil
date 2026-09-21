// Headless functional check that loads a real image FIXTURE from disk (via QImage) and
// runs it through the same shared code the GUI uses: load into CanvasWidget, take a
// crop, and apply the core image filter to the actual pixels. The desktop counterpart
// of the CLI's decode -> crop -> filter integration test (cli/tests/). Runs offscreen,
// returns non-zero on any failed expectation. Built only when Qt is present.
#include "CanvasWidget.hpp"
#include "imageFilter.hpp"
#include "iconSet.hpp"
#include "numericInput.hpp"
#include "IncognitoOverlay.hpp"
#include "theme.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QPixmap>
#include <QWidget>
#include <cstdio>
#include <cstdlib>
void accentInkAndNumericInput();

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

#include "support/check.hpp"

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
  check(canvas.getOriginalImage().width() == 16 && canvas.getOriginalImage().height() == 12,
        "original image kept at 16x12");
  const CropRect c0 = canvas.getCropRect();
  check(c0.width > 0 && c0.height > 0 && c0.width <= 16 && c0.height <= 12,
        "default crop is a non-empty sub-rect of the image");

  // Take an explicit 8x6 crop from the top-left and confirm it sticks.
  canvas.applyCrop(CropRect{0, 0, 8, 6}, /*recalc=*/true);
  check(canvas.imageWidth() == 8 && canvas.imageHeight() == 6,
        "working image is the 8x6 crop");

  // 3) Apply the core image filter (bw) to the real pixels and confirm it greyscales.
  QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
  stencil::core::applyFilterRGBA(stencil::core::FilterMode::BW, rgba.bits(),
                                 static_cast<std::size_t>(rgba.width()) * rgba.height(),
                                 0, 0, 0);
  const QRgb after = rgba.pixel(0, 0);  // RGBA8888 read back as ARGB QRgb
  std::printf("bw filter pixel: r=%d g=%d b=%d a=%d\n", qRed(after), qGreen(after),
              qBlue(after), qAlpha(after));
  check(qRed(after) == qGreen(after) && qGreen(after) == qBlue(after),
        "bw filter greyscaled the pixel (r==g==b)");
  check(qAlpha(after) == 255, "bw filter preserved alpha");

  // Contour through the real canvas cache path (rebuildFilteredImage → core::applyContourRGBA): a
  // uniform image has no edges, so every Sobel magnitude is 0 and the output is white, alpha preserved.
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

  // IncognitoOverlay: the viewport-pinned dashed accent frame (port of body.incognito-mode) must paint
  // FLUSH with the viewport edge and be transparent everywhere else, never becoming image content.
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
  // The frame DRAWS ON clockwise over DRAW_MS rather than blinking into place, so the first frame is
  // legitimately empty — pump the loop until it has closed before measuring.
  check(countNear(host.grab().toImage(), accent, 24) == 0,
        "the frame starts empty and draws on, rather than appearing all at once");
  {
    QElapsedTimer t; t.start();
    while (overlay->getProgress() < 1.0 && t.elapsed() < IncognitoOverlay::DRAW_MS * 4)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  check(overlay->getProgress() >= 1.0, "the frame reaches its closed state");
  const QImage shot = host.grab().toImage();  // composites overlay over the host
  const long onAccent = countNear(shot, accent, 24);
  const long bgShown = countNear(shot, host_bg, 16);
  std::printf("  host px: accent off=%ld on=%ld | backdrop-through=%ld\n",
              offAccent, onAccent, bgShown);
  // The solid 3px dashed frame contributes the clean-accent pixels; jumping clear
  // of zero proves the frame paints. Font-independent.
  check(onAccent > 40, "active overlay paints the dashed accent frame");
  // Flush, not inset: the 3px stroke sits ON the edge, so the OUTERMOST row and column carry dashes;
  // an inset frame leaves bare canvas outside them.
  {
    const auto rowHasAccent = [&](int y) {
      for (int x = 0; x < shot.width(); ++x)
        if (countNear(shot.copy(x, y, 1, 1), accent, 24)) return true;
      return false;
    };
    const auto colHasAccent = [&](int x) {
      for (int y = 0; y < shot.height(); ++y)
        if (countNear(shot.copy(x, y, 1, 1), accent, 24)) return true;
      return false;
    };
    check(rowHasAccent(0) && rowHasAccent(shot.height() - 1),
          "the frame reaches the top and bottom edges (no inset gap)");
    check(colHasAccent(0) && colHasAccent(shot.width() - 1),
          "…and the left and right edges");
    check(IncognitoOverlay::frameBox(QRectF(0, 0, 200, 100)) ==
              QRectF(1.5, 1.5, 197, 97),
          "the stroke box is inset only by the pen's half-width");
    // Nothing is painted INSIDE the frame: the old "Incognito — not saved" pill
    // covered the picture, and that fact now lives on the toolbar "?" instead.
    const int in = IncognitoOverlay::PEN_PX + 2;
    const QImage inner = shot.copy(in, in, shot.width() - 2 * in, shot.height() - 2 * in);
    check(countNear(inner, accent, 24) == 0,
          "no badge over the picture — only the frame paints");
  }
  // Every toggle animates, not just the first — the frame must draw on and retract
  // each time, never snap into place because some cached state short-circuits it.
  {
    const auto pump = [](int ms) {
      QElapsedTimer t; t.start();
      while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    };
    // Sample a quarter of the way in: mid-flight, so a snap shows up as 0 or 1.
    const auto cycle = [&](bool on) {
      overlay->setActive(on);
      pump(IncognitoOverlay::DRAW_MS / 4);
      const double mid = overlay->getProgress();
      pump(IncognitoOverlay::DRAW_MS * 2);
      return mid;
    };
    for (int round = 1; round <= 3; ++round) {
      const double off = cycle(false), on = cycle(true);
      std::printf("  toggle round %d: off-mid=%.2f on-mid=%.2f\n", round, off, on);
      check(off > 0.0 && off < 1.0, "the frame RETRACTS gradually on this toggle");
      check(on > 0.0 && on < 1.0, "the frame DRAWS ON gradually on this toggle");
    }
    overlay->setActive(true);
    pump(IncognitoOverlay::DRAW_MS * 2);
  }

  // Most of the host area must still read as backdrop — proving the overlay is
  // transparent (the canvas underneath would otherwise be hidden).
  check(bgShown > 320 * 200 / 2,
        "overlay is transparent — the canvas shows through everywhere but the frame");
  accentInkAndNumericInput();

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
