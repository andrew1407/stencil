// One committed (or in-flight) line and one point, painted into `r.ctx`; `r` is the Renderer (or
// the resting stand-in the result paint hands it). Desktop twin: CanvasWidget.cpp.
import { hexToRgba } from '../../utils/color.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

// px, the one dash table the desktop pen and the core rasteriser read too.
const { dashed: DASH_PATTERN, dotted: DOT_PATTERN } = constants.STROKE_DASH;
const { FOCUS_RING, HOVER_RING, SELECT_GLOW, MARKER_RING } = constants;

// Glow/ring colours are asked for once per selected line and highlighted point EVERY
// frame; the (colour, alpha) pairs are few, so memoize. The cap bounds a runaway accent sweep.
const RGBA = new Map();
const rgba = (hex, a) => {
  const key = hex + a;
  let v = RGBA.get(key);
  if (v === undefined) { if (RGBA.size > 64) RGBA.clear(); RGBA.set(key, v = hexToRgba(hex, a)); }
  return v;
};

// A point's 1 px ring on its canvas-normalised fill (core markers::ringFor): black from Rec. 709 luma
// MARKER_RING.darkFromLuma up, else white; a fill it cannot read keeps black.
const RINGS = new Map();
export const ringFor = (css) => {
  let v = RINGS.get(css);
  if (v !== undefined) return v;
  const s = String(css).trim();
  const hex = /^#([0-9a-f]{6})$/i.exec(s)?.[1]
    ?? /^#([0-9a-f]{3})$/i.exec(s)?.[1].replace(/./g, '$&$&');
  const fn = /^rgba?\(\s*(\d+)[\s,]+(\d+)[\s,]+(\d+)/i.exec(s);
  const rgb = hex ? [0, 2, 4].map((i) => parseInt(hex.slice(i, i + 2), 16)) : fn ? fn.slice(1, 4).map(Number) : null;
  const luma = rgb ? Math.trunc(0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]) : 255;
  v = luma >= MARKER_RING.darkFromLuma ? '#000' : '#fff';
  if (RINGS.size > 64) RINGS.clear();
  RINGS.set(css, v);
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
  const ctx = r.ctx;
  const fx = r.app.strokeFx;
  const pts = fx.pointsOf(line);
  if (line.points.length < 2) {
    if (line.points.length === 1 && r.app.showPoints) {
      const hs = r.pointHighlightState(lineIdx, 0);
      const ps = (line.pointSize ?? r.app.pointSize) * fx.scaleAt(line.points[0]);
      drawPoint(r, pts[0], pointColorOf(line), ps, isSelected, hs);
      fx.paintOver(ctx, line, pts);
    }
    return;
  }

// Locked-area fill, beneath stroke & glow.
  if (line.locked && line.points.length >= 3 && line.fillColor && line.fillColor !== 'transparent') {
    ctx.save();
    ctx.fillStyle = line.fillColor;
    tracePath(ctx, line, pts);
    ctx.fill();
    ctx.restore();
  }

// One glow pass beneath the stroke: the selection's, or the thinner Lines-list hover one.
  const glow = isSelected ? { color: r.app.selGlowColor, alpha: SELECT_GLOW.lineAlpha, pad: SELECT_GLOW.linePadPx }
    : (!r.suppressHighlight && lineIdx >= 0 && lineIdx === (r.app.listHoverLineIdx ?? -1)
      && line.points.length >= 2
      ? { color: r.app.hoverRingColor, alpha: SELECT_GLOW.lineHoverAlpha, pad: SELECT_GLOW.lineHoverPadPx } : null);
  if (glow) {
    ctx.save();
    ctx.strokeStyle = rgba(glow.color, glow.alpha);
    ctx.lineWidth = line.thickness + glow.pad;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.setLineDash([]);
    tracePath(ctx, line, pts);
    ctx.stroke();
    ctx.restore();
  }

  fx.paintUnder(ctx, line, pts);

  ctx.strokeStyle = line.color;
  ctx.lineWidth = line.thickness;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';

  if (line.style === 'dashed') {
    ctx.setLineDash(DASH_PATTERN);
  } else if (line.style === 'dotted') {
    ctx.setLineDash(DOT_PATTERN);
  } else {
    ctx.setLineDash([]);
  }

  tracePath(ctx, line, pts);

  ctx.stroke();
  ctx.setLineDash([]);

  if (r.app.showPoints) {
    pts.forEach((point, pi) => {
      const hs = r.pointHighlightState(lineIdx, pi);
      const ps = (line.pointSize ?? r.app.pointSize) * fx.scaleAt(line.points[pi]);
      drawPoint(r, point, pointColorOf(line), ps, isSelected, hs);
    });
  }
  fx.paintOver(ctx, line, pts);
}

// highlightState: 0 = none, 1 = hover (subtle ring), 2 = focused (bold ring + shadow)
export function drawPoint(r, point, color, pointSize = 4, isSelected = false, highlightState = 0) {
  const ctx = r.ctx;
  // ctx.arc throws IndexSizeError on a negative radius, which would stop every later paint.
  const rad = Math.max(0, pointSize);
  if (isSelected) {
    ctx.fillStyle = rgba(r.app.selGlowColor, SELECT_GLOW.pointAlpha);
    ctx.beginPath();
    ctx.arc(point.x, point.y, rad + SELECT_GLOW.pointGapPx, 0, Math.PI * 2);
    ctx.fill();
  }
  if (highlightState === 1) {
    ctx.save();
    ctx.strokeStyle = rgba(r.app.hoverRingColor, HOVER_RING.alpha);
    ctx.lineWidth = HOVER_RING.widthPx;
    ctx.beginPath();
    ctx.arc(point.x, point.y, rad + HOVER_RING.gapPx, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();
  } else if (highlightState === 2) {
    ctx.save();
    ctx.shadowColor = rgba(r.app.focusRingColor, FOCUS_RING.glowAlpha);
    ctx.shadowBlur = FOCUS_RING.blurPx;
    ctx.strokeStyle = r.app.focusRingColor;
    ctx.lineWidth = FOCUS_RING.widthPx;
    ctx.beginPath();
    ctx.arc(point.x, point.y, rad + FOCUS_RING.gapPx, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();
  }
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.arc(point.x, point.y, rad, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = ringFor(ctx.fillStyle);
  ctx.lineWidth = 1;
  ctx.stroke();
}
