// A repaint of part of the canvas strokes only the lines near it (CanvasPaint.cpp), and must
// still land every pixel a full repaint lands there — plain, filtered, split-compared, selected.
#include "CanvasWidget.hpp"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
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

  QRect changed(const QImage& a, const QImage& b) {
    QRect box;
    for (int y = 0; y < a.height(); ++y)
      for (int x = 0; x < a.width(); ++x)
        if (a.pixel(x, y) != b.pixel(x, y)) box |= QRect(x, y, 1, 1);
    return box;
  }

  // The union of the rects the widget asks to repaint while events drain.
  struct PaintLog : QObject {
    QRegion asked;
    bool eventFilter(QObject*, QEvent* e) override {
      if (e->type() == QEvent::Paint) asked += static_cast<QPaintEvent*>(e)->region();
      return false;
    }
  };
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

  // A hover change repaints only the rings it moved, and that is every pixel the ring changes.
  canvas.setCompareMode(QStringLiteral("none"));
  canvas.setImageFilter(QStringLiteral("none"), QColor("#7c3aed"));
  canvas.deselect();
  canvas.resize(450, 300);
  canvas.show();
  QApplication::processEvents();
  const QImage before = paint(canvas, QRegion(canvas.rect()));
  PaintLog log;   // installed between renders: render() itself sends a full Paint
  canvas.installEventFilter(&log);
  const QPointF onLine1(140 * 1.5, 78 * 1.5);
  QMouseEvent move(QEvent::MouseMove, onLine1, canvas.mapToGlobal(onLine1), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &move);
  QApplication::processEvents();
  const QRegion onHover = log.asked;
  canvas.removeEventFilter(&log);
  const QImage hovered = paint(canvas, QRegion(canvas.rect()));
  const QRect ring = changed(before, hovered);
  check(!ring.isEmpty(), "hovering a line draws its ring");
  check(onHover.contains(ring) && onHover.boundingRect() != canvas.rect(),
        "the hover repaint asks for the ring's rect, not the whole canvas");
  log.asked = QRegion();
  canvas.installEventFilter(&log);
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(&canvas, &leave);
  QApplication::processEvents();
  const QRegion onLeave = log.asked;
  canvas.removeEventFilter(&log);
  check(changed(before, paint(canvas, QRegion(canvas.rect()))).isEmpty(), "leaving takes the ring off again");
  check(onLeave.contains(ring) && onLeave.boundingRect() != canvas.rect(), "…repainting only where it was");

  // A hover paint culls by the bounds the last paint kept; an edit that keeps every line's point count
  // and width must still drop them, or the moved line is culled out of the ring's repaint.
  stencil::core::Lines moved = canvas.getLines();
  for (auto& pt : moved[1].points) pt.y += 110;
  canvas.setLines(moved);   // its repaint still queued, as the hover's lands first
  const QPointF onMoved(140 * 1.5, 188 * 1.5);
  QMouseEvent moveTo(QEvent::MouseMove, onMoved, canvas.mapToGlobal(onMoved), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &moveTo);
  const QRect nearCursor = QRectF(onMoved - QPointF(6, 6), QSizeF(12, 12)).toRect();
  const QImage cached = paint(canvas, QRegion(nearCursor));   // inside the rings the move asked for
  check(sameIn(cached, paint(canvas, QRegion(canvas.rect())), nearCursor),
        "a hover repaint after an edit draws the line where it now is");
  QApplication::processEvents();

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
