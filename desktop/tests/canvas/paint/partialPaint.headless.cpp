// A repaint of part of the canvas strokes only the lines near it (CanvasPaint.cpp), and must
// still land every pixel a full repaint lands there — plain, filtered, split-compared, selected.
#include "CanvasWidget.hpp"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QRegion>

#include "../../support/check.hpp"

using stencil::gui::CanvasWidget;

namespace {
  QImage paint(CanvasWidget& canvas, const QRegion& region) {
    QImage out(canvas.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::magenta);
    canvas.render(&out, region.boundingRect().topLeft(), region);   // the region lands where it sits
    return out;
  }

  bool sameIn(const QImage& a, const QImage& b, const QRect& r) {
    for (int y = r.top(); y <= r.bottom(); ++y)
      for (int x = r.left(); x <= r.right(); ++x)
        if (a.pixel(x, y) != b.pixel(x, y)) return false;
    return true;
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  CanvasWidget canvas;
  QImage page(300, 200, QImage::Format_ARGB32);
  page.fill(QColor(40, 120, 200));
  canvas.loadFromImage(page, stencil::core::CropRect{0, 0, 300, 200}, 0);
  canvas.setScale(1.5);
  stencil::core::Lines lines;
  const double rows[] = {20, 70, 130, 185};
  for (double y : rows) {
    stencil::core::Line l;
    l.points = {{15, y}, {140, y + 8}, {285, y - 6}};
    l.thickness = 5;
    l.color = "#ffdd00";
    lines.push_back(l);
  }
  lines[2].style = "dashed";
  lines[3].locked = true;
  lines[3].points = {{200, 150}, {280, 150}, {260, 195}};
  lines[3].fillColor = "#ff000080";
  canvas.setLines(lines);

  // A multi-selection glows every member (isLineSelected per line per frame), in any click order.
  canvas.toggleLineSelectionByIndex(3);
  canvas.toggleLineSelectionByIndex(0);
  canvas.toggleLineSelectionByIndex(2);
  check(canvas.isLineSelected(0) && !canvas.isLineSelected(1) && canvas.isLineSelected(2) &&
            canvas.isLineSelected(3) && canvas.selectedIndices() == std::vector<int>({0, 2, 3}),
        "a multi-selection holds exactly the lines toggled into it");
  canvas.toggleLineSelectionByIndex(2);
  check(!canvas.isLineSelected(2) && canvas.selectionCount() == 2, "…and toggling one drops it again");

  const QRect rects[] = {QRect(0, 0, 60, 60), QRect(180, 150, 120, 120), QRect(100, 90, 40, 40),
                         QRect(420, 280, 30, 20)};
  for (const char* compare : {"none", "vertical", "original"}) {
    for (const char* filter : {"none", "sepia"}) {
      canvas.setCompareMode(QString::fromLatin1(compare));
      canvas.setImageFilter(QString::fromLatin1(filter), QColor("#7c3aed"));
      const QImage full = paint(canvas, QRegion(canvas.rect()));
      bool same = true;
      for (const QRect& r : rects) same = same && sameIn(paint(canvas, QRegion(r)), full, r & canvas.rect());
      std::printf("  compare %s, filter %s\n", compare, filter);
      check(same, "a partial repaint lands the pixels a full one does");
    }
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
