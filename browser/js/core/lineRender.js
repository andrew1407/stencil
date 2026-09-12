// Ported from js/core/renderer.js: one committed (or in-flight) line and one point.
// `r` is the Renderer — these read r.app and r.pointHighlightState(), which the
// selection-glow and ring decisions both need. Desktop twin: canvasWidget.cpp.
import { hexToRgba } from '../utils.js';

// canvas setLineDash patterns for the two non-solid line styles.
const DASH_PATTERN = [10, 5];
const DOT_PATTERN = [2, 5];

// hexToRgba builds a fresh string per call, and the glow/ring colours are asked for once
// per selected line and once per highlighted point EVERY frame. The (colour, alpha) pairs
// are few and fixed, so memoize them; the cap keeps a runaway accent sweep bounded.
const RGBA = new Map();
const rgba = (hex, a) => {
  const key = hex + a;
  let v = RGBA.get(key);
  if (v === undefined) { if (RGBA.size > 64) RGBA.clear(); RGBA.set(key, v = hexToRgba(hex, a)); }
  return v;
};

// The colour a line's points draw in: its own pointColor when set, else its stroke
// colour. JS twin of core's pointColorOr (core/models.hpp) — keep the two identical;
// "unset" must include '' (how the field serialises when a line has no point colour).
export const pointColorOf = (line) => (line.pointColor ? line.pointColor : line.color);

// Trace the polygon through `pts`, closing it when the line is locked.
const tracePath = (ctx, line, pts) => {
  ctx.beginPath();
  ctx.moveTo(pts[0].x, pts[0].y);
  for (let i = 1; i < line.points.length; i++)
    ctx.lineTo(pts[i].x, pts[i].y);
  if (line.locked) ctx.closePath();
};

export function drawLine(r, line, isSelected = false, lineIdx = -99) {
  // Points in flight (a just-added vertex travelling to where it was put) are drawn
  // where they are RIGHT NOW; everything below — fill, glows, stroke, points — reads
  // this array, so the segments hanging off a moving vertex follow it for free.
  const fx = r.app.strokeFx;
  const pts = fx.pointsOf(line);
  if (line.points.length < 2) {
    if (line.points.length === 1 && r.app.showPoints) {
      const hs = r.pointHighlightState(lineIdx, 0);
      const ps = (line.pointSize ?? r.app.pointSize) * fx.scaleAt(line.points[0]);
      drawPoint(r, pts[0], pointColorOf(line), ps, isSelected, hs);
      fx.paintOver(r.app.ctx, line, pts);
    }
    return;
  }

  // Locked-area fill (closed polygon) — drawn beneath stroke & glow
  if (line.locked && line.points.length >= 3 && line.fillColor && line.fillColor !== 'transparent') {
    r.app.ctx.save();
    r.app.ctx.fillStyle = line.fillColor;
    tracePath(r.app.ctx, line, pts);
    r.app.ctx.fill();
    r.app.ctx.restore();
  }

  // One glow pass, drawn beneath the stroke: the selection's, or — on an unselected line —
  // the thinner, fainter one a Lines-list row hover paints, so the two stay distinguishable.
  const glow = isSelected ? { color: r.app.selGlowColor, alpha: 0.6, pad: 8 }
    : (!r.suppressHighlight && lineIdx >= 0 && lineIdx === (r.app.listHoverLineIdx ?? -1)
      && line.points.length >= 2 ? { color: r.app.hoverRingColor, alpha: 0.35, pad: 6 } : null);
  if (glow) {
    r.app.ctx.save();
    r.app.ctx.strokeStyle = rgba(glow.color, glow.alpha);
    r.app.ctx.lineWidth = line.thickness + glow.pad;
    r.app.ctx.lineCap = 'round';
    r.app.ctx.lineJoin = 'round';
    r.app.ctx.setLineDash([]);
    tracePath(r.app.ctx, line, pts);
    r.app.ctx.stroke();
    r.app.ctx.restore();
  }

  // The heat the flying vertex drags behind it, under the real stroke.
  fx.paintUnder(r.app.ctx, line, pts);

  r.app.ctx.strokeStyle = line.color;
  r.app.ctx.lineWidth = line.thickness;
  r.app.ctx.lineCap = 'round';
  r.app.ctx.lineJoin = 'round';

  if (line.style === 'dashed') {
    r.app.ctx.setLineDash(DASH_PATTERN);
  } else if (line.style === 'dotted') {
    r.app.ctx.setLineDash(DOT_PATTERN);
  } else {
    r.app.ctx.setLineDash([]);
  }

  tracePath(r.app.ctx, line, pts);

  r.app.ctx.stroke();
  r.app.ctx.setLineDash([]);

  if (r.app.showPoints) {
    pts.forEach((point, pi) => {
      const hs = r.pointHighlightState(lineIdx, pi);
      const ps = (line.pointSize ?? r.app.pointSize) * fx.scaleAt(line.points[pi]);
      drawPoint(r, point, pointColorOf(line), ps, isSelected, hs);
    });
  }
  // The spark riding a flying vertex and the ring its landing pushes out, over
  // everything else this line drew.
  fx.paintOver(r.app.ctx, line, pts);
}

// highlightState: 0 = none, 1 = hover (subtle ring), 2 = focused (bold ring + shadow)
export function drawPoint(r, point, color, pointSize = 4, isSelected = false, highlightState = 0) {
  const rad = pointSize;
  if (isSelected) {
    r.app.ctx.fillStyle = rgba(r.app.selGlowColor, 0.5);
    r.app.ctx.beginPath();
    r.app.ctx.arc(point.x, point.y, rad + 4, 0, Math.PI * 2);
    r.app.ctx.fill();
  }
  if (highlightState === 1) {
    // Hover — thin translucent ring
    r.app.ctx.save();
    r.app.ctx.strokeStyle = rgba(r.app.hoverRingColor, 0.55);
    r.app.ctx.lineWidth = 1.8;
    r.app.ctx.beginPath();
    r.app.ctx.arc(point.x, point.y, rad + 4, 0, Math.PI * 2);
    r.app.ctx.stroke();
    r.app.ctx.restore();
  } else if (highlightState === 2) {
    // Focused/click — bold ring with glow shadow
    r.app.ctx.save();
    r.app.ctx.shadowColor = rgba(r.app.focusRingColor, 0.9);
    r.app.ctx.shadowBlur = 12;
    r.app.ctx.strokeStyle = r.app.focusRingColor;
    r.app.ctx.lineWidth = 3;
    r.app.ctx.beginPath();
    r.app.ctx.arc(point.x, point.y, rad + 6, 0, Math.PI * 2);
    r.app.ctx.stroke();
    r.app.ctx.restore();
  }
  r.app.ctx.fillStyle = color;
  r.app.ctx.beginPath();
  r.app.ctx.arc(point.x, point.y, rad, 0, Math.PI * 2);
  r.app.ctx.fill();
  r.app.ctx.strokeStyle = '#000';
  r.app.ctx.lineWidth = 1;
  r.app.ctx.stroke();
}
