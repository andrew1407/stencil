#include "CanvasWidget.hpp"
#include "canvasPaintCache.hpp"
#include "CanvasWidget.hpp"
#include "theme.hpp"

#include <QPen>

// Painting one line: its fill, glow, stroke and points.

namespace stencil::gui {

  // Locked-area fill beneath the stroke (renderer.js): only for closed shapes
  // with a non-transparent fill while lines are shown.
  void CanvasWidget::drawFill(QPainter& p, const core::Line& line,
                              const QPolygonF& poly) const {
    if (showLines_ && line.locked && line.points.size() >= 3 &&
        line.fillColor != "transparent" && !line.fillColor.empty()) {
      p.setBrush(paintColor(line.fillColor));
      p.setPen(Qt::NoPen);
      p.drawPolygon(poly);
    }
  }

  // Selection glow beneath the stroke (renderer.js drawLine ~77): a fat, semi-
  // transparent halo around the selected committed line. Live view only.
  void CanvasWidget::drawGlow(QPainter& p, const core::Line& line,
                              const QPolygonF& poly, int lineIdx, bool highlight,
                              const Palette& pal) const {
    // Lines-list row hover: a thinner, fainter glow than the selection's, so the
    // two states stay distinguishable (browser renderer.js listHoverLineIdx).
    if (highlight && showLines_ && lineIdx >= 0 && lineIdx == listHoverLineIdx_ &&
        !isLineSelected(lineIdx) && line.points.size() >= 2) {
      QColor glow = pal.hoverRing;
      glow.setAlphaF(0.35);
      QPen gpen(glow);
      gpen.setWidthF(line.thickness + 6.0);
      gpen.setCapStyle(Qt::RoundCap);
      gpen.setJoinStyle(Qt::RoundJoin);
      p.setPen(gpen);
      p.setBrush(Qt::NoBrush);
      if (line.locked) p.drawPolygon(poly);
      else p.drawPolyline(poly);
    }
    if (highlight && showLines_ && lineIdx >= 0 && isLineSelected(lineIdx) &&
        line.points.size() >= 2) {
      QColor glow = pal.selGlow;
      glow.setAlphaF(0.6);
      QPen gpen(glow);
      gpen.setWidthF(line.thickness + 8.0);
      gpen.setCapStyle(Qt::RoundCap);
      gpen.setJoinStyle(Qt::RoundJoin);
      p.setPen(gpen);
      p.setBrush(Qt::NoBrush);
      if (line.locked) p.drawPolygon(poly);
      else p.drawPolyline(poly);
    }
  }

  // The stroke itself: width/cap/join + dash pattern per style.
  void CanvasWidget::drawStroke(QPainter& p, const core::Line& line,
                                const QPolygonF& poly,
                                const QColor& stroke) const {
    if (showLines_) {
      QPen pen(stroke);
      pen.setWidthF(line.thickness);
      pen.setCapStyle(Qt::RoundCap);
      pen.setJoinStyle(Qt::RoundJoin);
      if (line.style == "dashed") pen.setDashPattern({10.0, 5.0});
      else if (line.style == "dotted") pen.setDashPattern({2.0, 5.0});
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      if (line.points.size() >= 2) {
        if (line.locked) p.drawPolygon(poly);
        else p.drawPolyline(poly);
      }
    }
  }

  // Points + hover/focus rings. The browser draws no point labels on the canvas
  // (renderer.js), so neither do we. Rings are live-only (never baked into exports).
  void CanvasWidget::drawPoints(QPainter& p, const core::Line& line,
                                 const QPolygonF& poly, int lineIdx,
                                 bool highlight, const QColor& stroke,
                                 const Palette& pal, bool live) const {
    if (!showPoints_) return;

    const bool isActive = highlight && (&line == panelLine());
    const double r = line.pointSize;
    p.setPen(QPen(pal.textMain, 1));
    for (int i = 0; i < poly.size(); ++i) {
      const QPointF v = poly[i];
      // Focused point: filled selection glow + bold ring (renderer.js state 2,
      // focusRingColor — Settings-backed, browser DEFAULT_VISUALS.focusRingColor).
      if (isActive && i == selectedPoint_) {
        p.setBrush(pal.selGlow);
        p.setPen(QPen(focusRing_, 2));
        p.drawEllipse(v, r + 3, r + 3);
        p.setPen(QPen(pal.textMain, 1));
      }
      // Hovered point: thin translucent ring (renderer.js state 1). Skipped on the focused point,
      // which has the bolder ring above.
      else if (highlight &&
               ((lineIdx == hoverLineIdx_ && i == hoverPointIdx_) ||
                (isActive && i == listHoverPointIdx_) ||
                (lineIdx >= 0 && lineIdx == listHoverLineIdx_))) {
        QColor ring = pal.hoverRing;
        ring.setAlphaF(0.55);
        QPen rpen(ring);
        rpen.setWidthF(1.8);
        p.setBrush(Qt::NoBrush);
        p.setPen(rpen);
        p.drawEllipse(v, r + 4, r + 4);
        p.setPen(QPen(pal.textMain, 1));
      }
      const double vr = r * pointScaleAt(lineIdx, line, i, live);
      p.setBrush(stroke);
      p.drawEllipse(v, vr, vr);
    }
  }

}  // namespace stencil::gui
