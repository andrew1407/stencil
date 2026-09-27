#include "CanvasScene.hpp"
#include "liveMarks.hpp"

#include <QPainter>
#include <QPen>
#include <QPolygonF>

// The flight, painted (browser js/core/line/strokeFx.js): where a just-added vertex is this frame, the
// heat it drags and the spark it lands with. Live view only — `live` carries the flights.

namespace stencil::gui {

  void CanvasScene::flownPolygon(const core::Line& line, int lineIdx, double scale,
                                 const LiveMarks* live, QPolygonF& poly) const {
    poly.clear();
    poly.reserve(static_cast<int>(line.points.size()));
    const bool moving = live && live->flights->touches(lineIdx);
    const double now = moving ? live->now() : 0.0;
    for (const auto& pt : line.points) {
      QPointF at(pt.x, pt.y);
      if (moving) {
        if (const stroke::Flight* f = live->flights->at(lineIdx, pt)) {
          const stroke::Phase ph = stroke::phase(now - f->start, f->fly);
          if (ph.fly < 1.0) at = stroke::flyPoint(f->from, f->to, ph.fly, f->bow);
        }
      }
      poly << QPointF(at.x() * scale, at.y() * scale);
    }
  }

  // How much bigger than its resting size a vertex is drawn right now.
  double CanvasScene::pointScaleAt(int lineIdx, const core::Line& line, int ptIdx,
                                   const LiveMarks* live) const {
    if (!live || ptIdx < 0 || ptIdx >= static_cast<int>(line.points.size())) return 1.0;
    const stroke::Flight* f = live->flights->at(lineIdx, line.points[ptIdx]);
    if (!f) return 1.0;
    return stroke::vertexScale(stroke::phase(live->now() - f->start, f->fly));
  }

  // The heat a flying vertex drags behind it: a fat, faint stroke in the line's own
  // colour over the segments it is pulling, under the real one.
  void CanvasScene::drawStrokeWake(QPainter& p, const core::Line& line,
                                   const QPolygonF& poly, int lineIdx,
                                   const QColor& stroke, const LiveMarks& live) const {
    if (!showLines || !live.flights->touches(lineIdx)) return;
    const double now = live.now();
    for (int i = 0; i < static_cast<int>(line.points.size()); ++i) {
      const stroke::Flight* f = live.flights->at(lineIdx, line.points[i]);
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
  void CanvasScene::drawStrokeSpark(QPainter& p, const core::Line& line,
                                    const QPolygonF& poly, int lineIdx,
                                    const QColor& pointFill, const LiveMarks& live) const {
    if (!live.flights->touches(lineIdx)) return;
    const double now = live.now();
    const double r = line.pointSize;
    for (int i = 0; i < poly.size() && i < static_cast<int>(line.points.size()); ++i) {
      const stroke::Flight* f = live.flights->at(lineIdx, line.points[i]);
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

}  // namespace stencil::gui
