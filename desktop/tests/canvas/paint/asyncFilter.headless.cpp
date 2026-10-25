// The filtered picture paintEvent shows (canvas/paint/CanvasFilter.cpp): a small picture filters
// inline as before; a large one paints at once without waiting on the rebuild, adopts the pool's
// answer when it lands, and drops an answer whose filter was changed while it ran.
#include "CanvasWidget.hpp"

#include <QApplication>
#include <QImage>
#include <QThreadPool>

#include "../../support/check.hpp"

using stencil::gui::CanvasWidget;

namespace {
  QColor centre(CanvasWidget& canvas) {
    QImage out(canvas.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::magenta);
    canvas.render(&out);
    return out.pixelColor(out.width() / 2, out.height() / 2);
  }

  void settle() {
    for (int i = 0; i < 4; ++i) {
      QThreadPool::globalInstance()->waitForDone();
      QCoreApplication::processEvents();
    }
  }

  bool grey(const QColor& c) { return c.red() == c.green() && c.green() == c.blue(); }

  void load(CanvasWidget& canvas, int w, int h, const QColor& page) {
    QImage picture(w, h, QImage::Format_ARGB32);
    picture.fill(page);
    canvas.loadFromImage(picture, stencil::core::CropRect{0, 0, double(w), double(h)}, 0);
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  const QColor page(40, 120, 200);
  {
    CanvasWidget canvas;
    canvas.resize(400, 300);
    load(canvas, 300, 200, page);
    canvas.setImageFilter(QStringLiteral("bw"), QColor("#7c3aed"));
    check(grey(centre(canvas)), "a small picture shows its filter on the first paint");
  }
  {
    CanvasWidget canvas;
    canvas.resize(400, 300);
    load(canvas, 2100, 2000, page);
    settle();
    canvas.setImageFilter(QStringLiteral("bw"), QColor("#7c3aed"));
    check(centre(canvas) == page, "a large picture paints at once, unfiltered while the pool rebuilds");
    settle();
    const QColor bw = centre(canvas);
    check(grey(bw) && bw != page, "…and shows the filter once the pool answers");
    check(canvas.renderToImage(QStringLiteral("tint")).pixelColor(1050, 1000) == bw,
          "…the same pixels an export renders");

    canvas.setImageFilter(QStringLiteral("sepia"), QColor("#7c3aed"));
    check(centre(canvas) == bw, "a new filter keeps the last filtered picture up while it rebuilds");
    canvas.setImageFilter(QStringLiteral("bw"), QColor("#7c3aed"));
    centre(canvas);
    settle();
    centre(canvas);
    settle();
    check(centre(canvas) == bw, "an answer for a filter since replaced is dropped, the newest one shown");
  }
  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
