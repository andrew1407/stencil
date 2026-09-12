#include "canvasWidget.hpp"
#include "canvasWidget.hpp"
#include "../support/motionPrefs.hpp"

#include <QPen>

// The stroke's wake and spark, and the fly-in of a just-placed point.

namespace stencil::gui {

  QRect CanvasWidget::lineRect(int lineIdx) const {
    if (lineIdx < -1 || lineIdx >= static_cast<int>(lines_.size())) return {};
    const core::Line& line = lineIdx < 0 ? currentLine_ : lines_[lineIdx];
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
      if (const stroke::Flight* f = strokeFx_.at(lineIdx, pt)) {
        add(f->from.x(), f->from.y());
        const QPointF over = f->to + (f->to - f->from) * 0.07;
        add(over.x(), over.y());
      }
    }
    // Stroke half-width plus the wake's extra 7, the ripple's 4.2x point radius, the bow cap.
    const double pad = line.thickness + line.pointSize * 4.2 + stroke::BOW_MAX + 8.0;
    return QRectF((x0 - pad) * scale_, (y0 - pad) * scale_, (x1 - x0 + 2 * pad) * scale_,
                  (y1 - y0 + 2 * pad) * scale_)
        .toAlignedRect();
  }

  QRect CanvasWidget::dragRect() const {
    if (dragKind_ == DragKind::None) return {};
    QRect r = lineRect(dragLineIdx_);
    for (const auto& entry : dragMultiOrig_) r = r.united(lineRect(entry.first));
    return r;
  }

  QRect CanvasWidget::strokeFxRect() const {
    if (!strokeFx_.active()) return {};
    QRect r = strokeFx_.touches(-1) ? lineRect(-1) : QRect();
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
      if (strokeFx_.touches(i)) r = r.united(lineRect(i));
    return r;
  }

  void CanvasWidget::flownPolygon(const core::Line& line, int lineIdx, double scale,
                                  bool live, QPolygonF& poly) const {
    poly.clear();
    poly.reserve(static_cast<int>(line.points.size()));
    const bool moving = live && strokeFx_.touches(lineIdx);
    const double now = moving ? fxNow() : 0.0;
    for (const auto& pt : line.points) {
      QPointF at(pt.x, pt.y);
      if (moving) {
        if (const stroke::Flight* f = strokeFx_.at(lineIdx, pt)) {
          const stroke::Phase ph = stroke::phase(now - f->start, f->fly);
          if (ph.fly < 1.0) at = stroke::flyPoint(f->from, f->to, ph.fly, f->bow);
        }
      }
      poly << QPointF(at.x() * scale, at.y() * scale);
    }
  }

  // The heat a flying vertex drags behind it: a fat, faint stroke in the line's own
  // colour over the segments it is pulling, under the real one.
  void CanvasWidget::drawStrokeWake(QPainter& p, const core::Line& line,
                                    const QPolygonF& poly, int lineIdx,
                                    const QColor& stroke) const {
    if (!showLines_ || !strokeFx_.touches(lineIdx)) return;
    const double now = fxNow();
    for (int i = 0; i < static_cast<int>(line.points.size()); ++i) {
      const stroke::Flight* f = strokeFx_.at(lineIdx, line.points[i]);
      if (!f) continue;
      const double a = stroke::wake(stroke::phase(now - f->start, f->fly).span);
      if (a < 0.01) continue;
      QColor heat = stroke;
      heat.setAlphaF(a);
      QPen pen(heat);
      pen.setWidthF(line.thickness + 7.0);
      pen.setCapStyle(Qt::RoundCap);
      pen.setJoinStyle(Qt::RoundJoin);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      for (int j : {i - 1, i + 1}) {
        if (j < 0 || j >= poly.size()) continue;
        p.drawLine(poly[j], poly[i]);
      }
    }
  }

  // The glow riding the vertex, and the ring its landing pushes out — on top of
  // everything the line drew, in the colour its points are drawn in.
  void CanvasWidget::drawStrokeSpark(QPainter& p, const core::Line& line,
                                     const QPolygonF& poly, int lineIdx,
                                     const QColor& pointFill) const {
    if (!strokeFx_.touches(lineIdx)) return;
    const double now = fxNow();
    const double r = line.pointSize;
    for (int i = 0; i < poly.size() && i < static_cast<int>(line.points.size()); ++i) {
      const stroke::Flight* f = strokeFx_.at(lineIdx, line.points[i]);
      if (!f) continue;
      const stroke::Phase ph = stroke::phase(now - f->start, f->fly);
      if (ph.fly < 1.0) {
        const stroke::Ring sp = stroke::spark(ph.fly);
        QColor glow = pointFill;
        glow.setAlphaF(sp.alpha);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(poly[i], r * sp.scale, r * sp.scale);
      } else if (ph.ripple < 1.0) {
        const stroke::Ring rp = stroke::ripple(ph.ripple);
        QColor ring = pointFill;
        ring.setAlphaF(rp.alpha);
        QPen pen(ring);
        pen.setWidthF(std::max(1.0, r * 0.45 * (1.0 - ph.ripple)));
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(poly[i], r * rp.scale, r * rp.scale);
      }
    }
  }

  void CanvasWidget::resetStrokeFx() {
    strokeFx_.clear();
    fxTimer_.stop();
  }

  // Send a just-added vertex on its way and keep the frame timer running while it and
  // any other are still moving.
  void CanvasWidget::flyInPoint(int lineIdx, const core::Line& line, int ptIdx,
                                const QPointF* from) {
    if (!support::drawingMotionOk()) return;   // "Drawing animation" off, or nothing may move
    strokeFx_.flyIn(lineIdx, line, ptIdx, fxNow(), from);
    if (strokeFx_.active() && !fxTimer_.isActive()) fxTimer_.start();
  }

  void CanvasWidget::flyInPoints(int lineIdx, const core::Line& line, int startIdx,
                                 int count) {
    if (!support::drawingMotionOk()) return;
    strokeFx_.flyInRange(lineIdx, line, startIdx, count, fxNow());
    if (strokeFx_.active() && !fxTimer_.isActive()) fxTimer_.start();
  }

}  // namespace stencil::gui
