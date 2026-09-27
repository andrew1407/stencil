// A committed filter pick is one undo step on the strokes' own stack: undo and redo put the mode
// and the tint back and say so, a hover preview or a tint drag before the pick is no step, a pick
// that changes nothing pushes nothing, and a load's filter is what the floor keeps.
#include "CanvasWidget.hpp"

#include <QImage>
#include <cstdio>

#include "../support/check.hpp"

using stencil::gui::CanvasWidget;
namespace core = stencil::core;

namespace {
  const QColor TEAL{"#336699"};
  const QColor RED{"#cc2200"};

  core::Line stroke() {
    core::Line l;
    l.points = {{10, 10}, {60, 40}};
    return l;
  }

  bool shows(const CanvasWidget& c, const char* mode, const QColor& tint) {
    return c.getImageFilter() == QLatin1String(mode) && c.getFilterColor().name() == tint.name();
  }
}  // namespace

void filterCases() {
  QImage img(120, 80, QImage::Format_RGB32);
  img.fill(Qt::white);
  CanvasWidget canvas;
  int restored = 0, changed = 0;
  QString restoredMode;
  QObject::connect(&canvas, &CanvasWidget::filterRestored, [&](const QString& mode, const QColor&) {
    ++restored;
    restoredMode = mode;
  });
  QObject::connect(&canvas, &CanvasWidget::changed, [&] { ++changed; });

  std::printf("filter steps:\n");
  check(!canvas.commitFilter(QStringLiteral("sepia"), TEAL) && !canvas.canUndo(),
        "with no picture a pick is no step");
  canvas.setImageFilter(QStringLiteral("bw"), TEAL);   // what a load adopts before its picture lands
  canvas.loadFromImage(img, core::CropRect{0, 0, 120, 80}, 0);
  check(!canvas.canUndo(), "a load starts the stack");
  canvas.commitLines({stroke()});

  changed = 0;
  check(canvas.commitFilter(QStringLiteral("sepia"), TEAL) && changed == 1, "a mode pick is one step");
  canvas.undo();
  check(shows(canvas, "bw", TEAL) && canvas.getLines().size() == 1, "undo puts the filter back, lines kept");
  check(restored == 1 && restoredMode == QLatin1String("bw"), "…and says which, for the window's controls");
  canvas.redo();
  check(shows(canvas, "sepia", TEAL) && restored == 2, "redo picks it again");

  std::printf("previews and drags:\n");
  canvas.setImageFilter(QStringLiteral("invert"), TEAL);   // a hover preview
  check(canvas.commitFilter(QStringLiteral("invert"), TEAL), "a pick the preview already shows is still a step");
  for (const char* hex : {"#110000", "#550000", "#990000"})
    canvas.setImageFilter(QStringLiteral("custom"), QColor(hex));   // the tint mid-drag
  check(canvas.commitFilter(QStringLiteral("custom"), RED), "the tint's release is one step");
  canvas.undo();
  check(shows(canvas, "invert", TEAL), "one undo skips every value the drag passed through");
  canvas.redo();

  std::printf("no-ops:\n");
  changed = 0;
  check(!canvas.commitFilter(QStringLiteral("custom"), RED) && changed == 0, "the same pick again is no step");
  canvas.setImageFilter(QStringLiteral("contour"), RED);   // previewed, then the list left alone
  check(!canvas.commitFilter(QStringLiteral("custom"), RED) && shows(canvas, "custom", RED),
        "picking the step's own filter after a preview is no step, and shows it again");
  restored = 0;
  canvas.commitLines({stroke(), stroke()});
  canvas.undo();
  check(restored == 0 && shows(canvas, "custom", RED), "undoing a stroke leaves the filter it sat on");

  std::printf("the floor:\n");
  while (canvas.canUndo()) canvas.undo();
  check(canvas.getLines().empty() && shows(canvas, "bw", TEAL), "the floor keeps the load's filter");
  while (canvas.canRedo()) canvas.redo();
  check(canvas.getLines().size() == 2 && shows(canvas, "custom", RED), "and redo walks back up to the last pick");
}
