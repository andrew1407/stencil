// The canvas's undo steps are editor mementos (core EditorHistory, the browser's editorMemento):
// the lines, the crop, turn and mirror they sit on and the filter, so a crop, a quarter-turn and a
// flip undo and redo like a stroke. A load's own crop starts the stack. Runs offscreen; the filter's own cases
// are canvasHistoryFilter.headless.cpp.
#include "CanvasWidget.hpp"

#include <QApplication>
#include <QImage>
#include <cstdio>

#include "../support/check.hpp"

using stencil::gui::CanvasWidget;
namespace core = stencil::core;

void filterCases();

namespace {
  bool sameCrop(const core::CropRect& a, const core::CropRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  }

  core::Line lineAt(double x) {
    core::Line l;
    l.points = {{x, 10}, {x + 40, 50}};
    return l;
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM

  QImage img(300, 200, QImage::Format_RGB32);
  img.fill(Qt::white);
  CanvasWidget canvas;
  canvas.loadFromImage(img, core::CropRect{0, 0, 300, 200}, 0);
  check(!canvas.canUndo() && !canvas.canRedo(), "a fresh load has nothing to undo");
  canvas.commitLines({lineAt(20)});
  const core::CropRect full = canvas.getCropRect();

  std::printf("crop:\n");
  canvas.applyCrop(core::CropRect{50, 20, 200, 150}, /*recalc=*/true);
  const core::CropRect cropped = canvas.getCropRect();
  const core::Lines scaled = canvas.getLines();
  check(sameCrop(cropped, {50, 20, 200, 150}) && canvas.imageWidth() == 200, "the crop lands");
  check(canvas.canUndo(), "…as an undo step");
  canvas.undo();
  check(sameCrop(canvas.getCropRect(), full) && canvas.imageWidth() == 300 && canvas.imageHeight() == 200,
        "undo restores the uncropped view");
  check(canvas.getLines().size() == 1 && canvas.getLines()[0].points[0].x == 20,
        "…and the lines as they were before the crop");
  canvas.redo();
  check(sameCrop(canvas.getCropRect(), cropped) && canvas.imageWidth() == 200, "redo crops again");
  check(canvas.getLines().size() == 1 && canvas.getLines()[0].points[0].x == scaled[0].points[0].x,
        "…with the lines the crop left");

  std::printf("rotate:\n");
  canvas.rotateImage(/*clockwise=*/true);
  check(canvas.getRotationQuarters() == 1 && canvas.imageWidth() == 150 && canvas.imageHeight() == 200,
        "a quarter turn swaps the view");
  canvas.undo();
  check(canvas.getRotationQuarters() == 0 && sameCrop(canvas.getCropRect(), cropped) && canvas.imageWidth() == 200,
        "undo turns it back, crop and all");
  check(canvas.getLines()[0].points[0].x == scaled[0].points[0].x, "…with the unturned lines");
  canvas.redo();
  check(canvas.getRotationQuarters() == 1 && canvas.imageHeight() == 200, "redo turns it again");
  canvas.undo();
  canvas.undo();
  check(sameCrop(canvas.getCropRect(), full) && canvas.getLines().size() == 1, "undo walks back past the crop to the stroke");
  canvas.undo();
  check(canvas.getLines().empty() && sameCrop(canvas.getCropRect(), full) && !canvas.canUndo(),
        "…and to the load, on the load's own view");

  std::printf("flip:\n");
  canvas.applyCrop(core::CropRect{20, 20, 200, 150}, /*recalc=*/true);
  canvas.commitLines({lineAt(30)});
  canvas.flipImage();
  check(canvas.getMirrored() && sameCrop(canvas.getCropRect(), {80, 20, 200, 150}) && canvas.imageWidth() == 200,
        "a flip mirrors the crop across the picture and keeps the view's size");
  check(canvas.getLines()[0].points[0].x == 170 && canvas.getLines()[0].points[1].x == 130,
        "…and the lines inside the view: x → width − x");
  canvas.rotateImage(/*clockwise=*/true);
  canvas.flipImage();
  check(!canvas.getMirrored() && canvas.getRotationQuarters() == 3, "a flip negates the turn");
  canvas.undo();
  canvas.undo();
  canvas.undo();
  check(!canvas.getMirrored() && canvas.getRotationQuarters() == 0 && sameCrop(canvas.getCropRect(), {20, 20, 200, 150}),
        "undo walks the flips and the turn back");
  canvas.undo();
  canvas.undo();

  std::printf("what starts the stack:\n");
  canvas.applyCrop(core::CropRect{0, 0, 120, 80}, /*recalc=*/false);
  check(!canvas.canUndo() && !canvas.canRedo(), "a load's placed crop (no recalc) is the new floor, not a step");
  canvas.commitLines({lineAt(5)});
  canvas.setFilter(QStringLiteral("sepia"));
  canvas.undo();
  check(canvas.getLines().empty() && canvas.getImageFilter() == QStringLiteral("none"),
        "an adopted filter is no step of its own: undo lands on the step below, filter and all");

  filterCases();

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
