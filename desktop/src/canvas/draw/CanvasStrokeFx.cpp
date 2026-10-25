#include "CanvasWidget.hpp"
#include "../../support/motionPrefs.hpp"

// The fly-in of a just-placed point, the widget rects a frame repaints, and the live marks the
// scene's paint path draws the flights and highlights from.

namespace stencil::gui {

  namespace {
    // Image-space bounds of a line with points, padded for its stroke, rings and any flight.
    QRectF paddedBounds(const core::Line& line, const stroke::Fx& fx, int lineIdx) {
      double x0 = line.points[0].x, y0 = line.points[0].y, x1 = x0, y1 = y0;
      const auto add = [&](double x, double y) {
        x0 = std::min(x0, x); y0 = std::min(y0, y);
        x1 = std::max(x1, x); y1 = std::max(y1, y);
      };
      const bool flying = fx.touches(lineIdx);
      for (const core::Point& pt : line.points) {
        add(pt.x, pt.y);
        if (!flying) continue;
        // A vertex in flight starts off the line (its anchor or its foot on the segment it
        // split) and eases PAST its target before settling; the bow is covered by the pad.
        if (const stroke::Flight* f = fx.at(lineIdx, pt)) {
          add(f->from.x(), f->from.y());
          const QPointF over = f->to + (f->to - f->from) * 0.07;
          add(over.x(), over.y());
        }
      }
      // Stroke half-width plus the wake's extra 7, the ripple's 4.2x point radius, the bow cap.
      const double pad = line.thickness + line.pointSize * 4.2 + stroke::BOW_MAX + 8.0;
      return {x0 - pad, y0 - pad, x1 - x0 + 2 * pad, y1 - y0 + 2 * pad};
    }

    QRect toWidget(const QRectF& image, double scale) {
      return QRectF(image.x() * scale, image.y() * scale, image.width() * scale, image.height() * scale)
          .toAlignedRect();
    }
  }  // namespace

  QRect CanvasWidget::lineRect(int lineIdx) const {
    if (lineIdx < -1 || lineIdx >= static_cast<int>(lines.size())) return {};
    const core::Line& line = lineIdx < 0 ? currentLine : lines[lineIdx];
    if (line.points.empty()) return {};
    return toWidget(paddedBounds(line, strokeFx, lineIdx), scale);
  }

  QRect CanvasWidget::paintRect(int lineIdx) const {
    // The line a gesture, a hold or a continuation edits, and one in flight, change between paints unannounced.
    if (lineIdx < 0 || lineIdx >= static_cast<int>(lines.size()) || !gesture.is(Gesture::NONE) ||
        hold.timer.isActive() || lineIdx == continueLineIdx || strokeFx.touches(lineIdx))
      return lineRect(lineIdx);
    if (!bounds.valid || bounds.lines.size() != lines.size()) {
      bounds.lines.assign(lines.size(), {});
      bounds.valid = true;
    }
    LineBounds::Entry& e = bounds.lines[lineIdx];
    const core::Line& line = lines[lineIdx];
    if (e.image.isNull() || e.points != line.points.size() || e.thickness != line.thickness ||
        e.pointSize != line.pointSize) {
      if (line.points.empty()) return {};
      e = {paddedBounds(line, strokeFx, lineIdx), line.points.size(), line.thickness, line.pointSize};
    }
    return toWidget(e.image, scale);
  }

  QRect CanvasWidget::hoverDirty(int wasHover, int wasOver) const {
    QRect dirty;
    for (int idx : {wasHover, wasOver, hover.lineIdx, hover.overLineIdx}) dirty = dirty.united(lineRect(idx));
    return dirty;
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
                                const QPointF* from, double holdMs) {
    if (!support::isDrawingMotionOk()) return;   // "Drawing animation" off, or nothing may move
    strokeFx.flyIn(lineIdx, line, ptIdx, fxNow(), from, holdMs);
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
