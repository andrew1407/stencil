#include "CanvasScene.hpp"
#include "canvasPaintCache.hpp"
#include "liveMarks.hpp"
#include "markMetrics.hpp"
#include "strokeDash.hpp"

#include <QPen>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

// Painting one line: its fill, glow, stroke and points (browser renderer.js). The live view's marks
// ride in `live`; an export passes none.

namespace stencil::gui {

  namespace {
    // The browser's shadowBlur under a ring stroke: the ring's band blurred by a Gaussian of sigma
    // blur / 2, so a stop at distance d from the ring reads alpha * (Phi((d + w/2)/s) - Phi((d - w/2)/s)).
    void paintRingGlow(QPainter& p, const QPointF& c, double ringR, double widthPx, double blurPx,
                       QColor color, double alpha) {
      const double s = blurPx / 2.0;
      const double reach = widthPx / 2.0 + 3.0 * s;
      const double outer = ringR + reach;
      if (s <= 0 || outer <= 0) return;
      const double inner = std::max(0.0, ringR - reach);
      const double sr2 = s * std::sqrt(2.0);
      QRadialGradient g(c, outer);
      constexpr int STOPS = 16;
      for (int i = 0; i <= STOPS; ++i) {
        const double rad = inner + (outer - inner) * i / STOPS;
        const double d = rad - ringR;
        const double k = 0.5 * (std::erf((d + widthPx / 2.0) / sr2) - std::erf((d - widthPx / 2.0) / sr2));
        color.setAlphaF(std::clamp(alpha * k, 0.0, 1.0));
        g.setColorAt(rad / outer, color);
      }
      p.setPen(Qt::NoPen);
      p.setBrush(g);
      p.drawEllipse(c, outer, outer);
    }
  }  // namespace

  // Port of the line drawing in browser/js/core/draw/renderer.js. Scale-parameterized so renderToImage
  // draws at native resolution while the live view passes scale.
  void CanvasScene::drawLineScaled(QPainter& p, const core::Line& line,
                                   int lineIdx, double scale,
                                   bool highlight, const LiveMarks* live) const {
    if (line.points.empty()) return;

    // A vertex added a moment ago is drawn where it is RIGHT NOW, so segments hanging off a moving
    // vertex follow it for free. One buffer per frame: QPolygonF keeps its capacity.
    static thread_local QPolygonF polyBuf;
    flownPolygon(line, lineIdx, 1.0, live, polyBuf);
    const QPolygonF& poly = polyBuf;

    const QColor stroke = paintColor(line.color);
    const Palette& pal = pinnedPalette ? *pinnedPalette : paintPalette(dark, accentKey, selGlow, hoverRing);

    // core::pointColorOr resolves an unset point colour to the stroke colour, so a line without one
    // paints as it always did; an unparseable colour falls back there too, never to black.
    const QColor pointParsed = paintColor(core::pointColorOr(line));
    const QColor pointFill = pointParsed.isValid() ? pointParsed : stroke;

    // A width and a radius are IMAGE px, as they are on the browser's CSS-scaled canvas: under
    // the transform they shrink with the zoom instead of fattening as the view pulls back.
    p.save();
    p.scale(scale, scale);
    drawFill(p, line, poly);
    drawGlow(p, line, poly, lineIdx, highlight, pal, live);
    if (live) drawStrokeWake(p, line, poly, lineIdx, stroke, *live);
    drawStroke(p, line, poly, stroke);
    drawPoints(p, line, poly, lineIdx, highlight, pointFill, pal, live);
    if (live) drawStrokeSpark(p, line, poly, lineIdx, pointFill, *live);
    p.restore();
  }

  // Locked-area fill beneath the stroke (renderer.js): only for closed shapes
  // with a non-transparent fill while lines are shown.
  void CanvasScene::drawFill(QPainter& p, const core::Line& line,
                             const QPolygonF& poly) const {
    if (showLines && line.locked && line.points.size() >= 3 &&
        line.fillColor != "transparent" && !line.fillColor.empty()) {
      p.setBrush(paintColor(line.fillColor));
      p.setPen(Qt::NoPen);
      p.drawPolygon(poly);
    }
  }

  // Selection glow beneath the stroke (renderer.js drawLine ~77): a fat, semi-
  // transparent halo around the selected committed line. Live view only.
  void CanvasScene::drawGlow(QPainter& p, const core::Line& line,
                             const QPolygonF& poly, int lineIdx, bool highlight,
                             const Palette& pal, const LiveMarks* live) const {
    // Lines-list row hover: a thinner, fainter glow than the selection's, so the
    // two states stay distinguishable (browser renderer.js listHoverLineIdx).
    const markMetrics::Table& m = markMetrics::table();
    if (highlight && showLines && lineIdx >= 0 && lineIdx == live->hover.listLineIdx &&
        !live->isSelected(lineIdx) && line.points.size() >= 2) {
      QColor glow = pal.hoverRing;
      glow.setAlphaF(m.lineHoverAlpha);
      QPen gpen(glow);
      gpen.setWidthF(line.thickness + m.lineHoverPadPx);
      gpen.setCapStyle(Qt::RoundCap);
      gpen.setJoinStyle(Qt::RoundJoin);
      p.setPen(gpen);
      p.setBrush(Qt::NoBrush);
      if (line.locked) p.drawPolygon(poly);
      else p.drawPolyline(poly);
    }
    if (highlight && showLines && lineIdx >= 0 && live->isSelected(lineIdx) &&
        line.points.size() >= 2) {
      QColor glow = pal.selGlow;
      glow.setAlphaF(m.lineGlowAlpha);
      QPen gpen(glow);
      gpen.setWidthF(line.thickness + m.lineGlowPadPx);
      gpen.setCapStyle(Qt::RoundCap);
      gpen.setJoinStyle(Qt::RoundJoin);
      p.setPen(gpen);
      p.setBrush(Qt::NoBrush);
      if (line.locked) p.drawPolygon(poly);
      else p.drawPolyline(poly);
    }
  }

  // The stroke itself: width/cap/join + dash pattern per style.
  void CanvasScene::drawStroke(QPainter& p, const core::Line& line,
                               const QPolygonF& poly,
                               const QColor& stroke) const {
    if (showLines) {
      QPen pen(stroke);
      pen.setWidthF(line.thickness);
      pen.setCapStyle(Qt::RoundCap);
      pen.setJoinStyle(Qt::RoundJoin);
      if (!strokeDash::patternPx(line.style).isEmpty())
        pen.setDashPattern(strokeDash::penPattern(line.style, line.thickness));
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
  void CanvasScene::drawPoints(QPainter& p, const core::Line& line,
                               const QPolygonF& poly, int lineIdx,
                               bool highlight, const QColor& stroke,
                               const Palette& pal, const LiveMarks* live) const {
    if (!showPoints) return;

    const bool isActive = highlight && (&line == live->panelLine);
    const double r = line.pointSize;
    const markMetrics::Table& m = markMetrics::table();
    p.setPen(QPen(pal.textMain, 1));
    for (int i = 0; i < poly.size(); ++i) {
      const QPointF v = poly[i];
      // Focused point: the selection disc, then a bold ring with its glow (render.js drawPoint
      // state 2, focusRingColor — Settings-backed, browser DEFAULT_VISUALS.focusRingColor).
      if (isActive && i == live->selectedPoint) {
        QColor disc = pal.selGlow;
        disc.setAlphaF(m.pointGlowAlpha);
        p.setPen(Qt::NoPen);
        p.setBrush(disc);
        p.drawEllipse(v, r + m.pointGlowGapPx, r + m.pointGlowGapPx);
        paintRingGlow(p, v, r + m.focusGapPx, m.focusWidthPx, m.focusBlurPx, focusRing, m.focusGlowAlpha);
        QPen ring(focusRing);
        ring.setWidthF(m.focusWidthPx);
        p.setPen(ring);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(v, r + m.focusGapPx, r + m.focusGapPx);
        p.setPen(QPen(pal.textMain, 1));
      }
      // Hovered point: thin translucent ring (renderer.js state 1). Skipped on the focused point,
      // which has the bolder ring above.
      else if (highlight &&
               ((lineIdx == live->hover.lineIdx && i == live->hover.pointIdx) ||
                (isActive && i == live->hover.listPointIdx) ||
                (lineIdx >= 0 && lineIdx == live->hover.listLineIdx))) {
        QColor ring = pal.hoverRing;
        ring.setAlphaF(m.hoverAlpha);
        QPen rpen(ring);
        rpen.setWidthF(m.hoverWidthPx);
        p.setBrush(Qt::NoBrush);
        p.setPen(rpen);
        p.drawEllipse(v, r + m.hoverGapPx, r + m.hoverGapPx);
        p.setPen(QPen(pal.textMain, 1));
      }
      const double vr = r * pointScaleAt(lineIdx, line, i, live);
      p.setBrush(stroke);
      p.drawEllipse(v, vr, vr);
    }
  }

}  // namespace stencil::gui
