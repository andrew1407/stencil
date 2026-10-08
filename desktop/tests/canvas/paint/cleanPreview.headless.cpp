// The logo drag's clean view (CanvasScene::setCleanPreview, browser twin ui/drag/logoDrag.js): the
// live paint shows the bare picture — no filter, line, point or compare split — while every setting
// and the history stay, an export and a pool-thread render copy never see it, and turned off the
// paint is back pixel for pixel.
#include "CanvasWidget.hpp"

#include <QApplication>
#include <QImage>

#include "../../support/check.hpp"

using stencil::gui::CanvasWidget;

namespace {
  QImage paint(CanvasWidget& canvas) {
    QImage out(canvas.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::magenta);
    canvas.render(&out);
    return out;
  }

  bool bare(const QImage& shot, const QColor& page) {
    for (int y = 0; y < shot.height(); ++y)
      for (int x = 0; x < shot.width(); ++x)
        if (shot.pixelColor(x, y) != page) return false;
    return true;
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  CanvasWidget canvas;
  const QColor page(40, 120, 200);
  QImage picture(300, 200, QImage::Format_ARGB32);
  picture.fill(page);
  canvas.loadFromImage(picture, stencil::core::CropRect{0, 0, 300, 200}, 0);
  stencil::core::Line line;
  line.points = {{20, 30}, {150, 90}, {280, 170}};
  line.thickness = 6;
  line.color = "#ffdd00";
  canvas.setLines({line});
  canvas.setImageFilter(QStringLiteral("sepia"), QColor("#7c3aed"));
  canvas.setCompareMode(QStringLiteral("vertical"));

  const QImage full = paint(canvas);
  const QImage exported = canvas.renderToImage(QStringLiteral("current"));
  const bool undoable = canvas.canUndo();
  check(!bare(full, page), "the paint carries the filter, the line, its points and the split");

  canvas.setCleanPreview(true);
  check(canvas.getCleanPreview() && bare(paint(canvas), page), "the clean view paints the bare picture alone");
  check(canvas.getImageFilter() == QLatin1String("sepia") && canvas.getCompareMode() == QLatin1String("vertical"),
        "…and leaves the filter and the compare view as they were");
  check(canvas.getLines().size() == 1 && canvas.canUndo() == undoable, "…and the lines and the history");
  check(canvas.renderToImage(QStringLiteral("current")) == exported, "an export never sees it");
  const auto copy = canvas.renderCopy();
  check(!copy->getCleanPreview() && copy->renderToImage(true) == exported, "nor does a render copy");

  canvas.setCleanPreview(false);
  check(!canvas.getCleanPreview() && paint(canvas) == full, "turned off, the paint is back pixel for pixel");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
