// One committed (or in-flight) line and one point; `r` is the Renderer. Desktop twin: canvasWidget.cpp.
import { hexToRgba } from '../utils.js';

const DASH_PATTERN = [10, 5];
const DOT_PATTERN = [2, 5];

// Glow/ring colours are asked for once per selected line and highlighted point EVERY
// frame; the (colour, alpha) pairs are few, so memoize. The cap bounds a runaway accent sweep.
const RGBA = new Map();
const rgba = (hex, a) => {
  const key = hex + a;
  let v = RGBA.get(key);
  if (v === undefined) { if (RGBA.size > 64) RGBA.clear(); RGBA.set(key, v = hexToRgba(hex, a)); }
  return v;
};

// JS twin of core's pointColorOr (core/models.hpp) — keep identical; "unset" includes ''.
export const pointColorOf = (line) => (line.pointColor ? line.pointColor : line.color);

const tracePath = (ctx, line, pts) => {
  ctx.beginPath();
  ctx.moveTo(pts[0].x, pts[0].y);
  for (let i = 1; i < line.points.length; i++)
    ctx.lineTo(pts[i].x, pts[i].y);
  if (line.locked) ctx.closePath();
};

export function drawLine(r, line, isSelected = false, lineIdx = -99) {
// Points in flight are drawn where they are RIGHT NOW: fill, glows, stroke and points all
// read this array, so segments hanging off a moving vertex follow it.
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

// Locked-area fill, beneath stroke & glow.
  if (line.locked && line.points.length >= 3 && line.fillColor && line.fillColor !== 'transparent') {
    r.app.ctx.save();
    r.app.ctx.fillStyle = line.fillColor;
    tracePath(r.app.ctx, line, pts);
    r.app.ctx.fill();
    r.app.ctx.restore();
  }

// One glow pass beneath the stroke: the selection's, or the thinner Lines-list hover one.
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
