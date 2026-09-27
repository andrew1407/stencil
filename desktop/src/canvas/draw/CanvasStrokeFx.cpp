#include "CanvasWidget.hpp"
#include "../../support/motionPrefs.hpp"

// The fly-in of a just-placed point, the widget rects a frame repaints, and the live marks the
// scene's paint path draws the flights and highlights from.

namespace stencil::gui {

  QRect CanvasWidget::lineRect(int lineIdx) const {
    if (lineIdx < -1 || lineIdx >= static_cast<int>(lines.size())) return {};
    const core::Line& line = lineIdx < 0 ? currentLine : lines[lineIdx];
    if (line.points.empty()) return {};
    double x0 = line.points[0].x, y0 = line.points[0].y, x1 = x0, y1 = y0;
    const auto add = [&](double x, double y) {
      x0 = std::min(x0, x); y0 = std::min(y0, y);
      x1 = std::max(x1, x); y1 = std::max(y1, y);
    };
    for (const core::Point& pt : line.points) {
      add(pt.x, pt.y);
      // A vertex in flight starts off the line (its anchor or its foot on the segment it
      // split) and eases PAST its target before settling; the bow is covered by the pad.
      if (const stroke::Flight* f = strokeFx.at(lineIdx, pt)) {
        add(f->from.x(), f->from.y());
        const QPointF over = f->to + (f->to - f->from) * 0.07;
        add(over.x(), over.y());
      }
    }
    // Stroke half-width plus the wake's extra 7, the ripple's 4.2x point radius, the bow cap.
    const double pad = line.thickness + line.pointSize * 4.2 + stroke::BOW_MAX + 8.0;
    return QRectF((x0 - pad) * scale, (y0 - pad) * scale, (x1 - x0 + 2 * pad) * scale,
                  (y1 - y0 + 2 * pad) * scale)
        .toAlignedRect();
  }

  QRect CanvasWidget::dragRect() const {
    if (!gesture.dragging()) return {};
    QRect r = lineRect(gesture.lineIdx);
    for (const auto& entry : gesture.multiOrig) r = r.united(lineRect(entry.first));
    return r;
  }

  QRect CanvasWidget::strokeFxRect() const {
    if (!strokeFx.active()) return {};
    QRect r = strokeFx.touches(-1) ? lineRect(-1) : QRect();
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
      if (strokeFx.touches(i)) r = r.united(lineRect(i));
    return r;
  }

  LiveMarks CanvasWidget::liveMarks() const {
    return {selectedIndices(), panelLine(), selectedPoint, hover, &strokeFx, &fxClock};
  }

  void CanvasWidget::resetStrokeFx() {
    strokeFx.clear();
    fxTimer.stop();
  }

  // Send a just-added vertex on its way and keep the frame timer running while it and
  // any other are still moving.
  void CanvasWidget::flyInPoint(int lineIdx, const core::Line& line, int ptIdx,
                                const QPointF* from) {
    if (!support::isDrawingMotionOk()) return;   // "Drawing animation" off, or nothing may move
    strokeFx.flyIn(lineIdx, line, ptIdx, fxNow(), from);
    if (strokeFx.active() && !fxTimer.isActive()) fxTimer.start();
  }

  void CanvasWidget::flyInPoints(int lineIdx, const core::Line& line, int startIdx,
                                 int count) {
    if (!support::isDrawingMotionOk()) return;
    strokeFx.flyInRange(lineIdx, line, startIdx, count, fxNow());
    if (strokeFx.active() && !fxTimer.isActive()) fxTimer.start();
  }

  // ms on the shared clock. Every pass takes it here so they all agree on one instant.
  double CanvasWidget::fxNow() const {
    return static_cast<double>(fxClock.elapsed());
  }

}  // namespace stencil::gui
