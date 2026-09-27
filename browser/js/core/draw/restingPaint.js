// The resting annotations an export, a thumbnail and the co-edit result carry: every line (or
// its points alone) with no ring, glow or vertex in flight. One loop for the editor's renderer
// and for the stand-in the image worker paints through, so the two cannot drift.
import { drawLine, drawPoint } from '../line/render.js';

// Nothing in flight: the resting array, full-size points, no wake or spark.
const RESTING_FX = Object.freeze({
  pointsOf: (line) => line.points, scaleAt: () => 1, paintUnder() {}, paintOver() {},
});

// `r` draws a line or a point (the Renderer, or restingPainter below); the lines are CodecLines.
export const paintRestingLines = (r, lines, { showLines, showPoints, pointSize }) => {
  if (showLines) {
    lines.forEach((line) => r.drawLine(line, false));
  } else if (showPoints) {
    lines.forEach((line) => {
      line.points.forEach((p) => r.drawPoint(p, line.color, line.pointSize ?? pointSize, false));
    });
  }
};

// A painter over any 2D context that needs no editor: what the image worker draws with.
export const restingPainter = (ctx, { showPoints, pointSize }) => {
  const r = {
    ctx,
    suppressHighlight: true,
    app: { strokeFx: RESTING_FX, showPoints, pointSize },
    pointHighlightState: () => 0,
    drawLine: (line, isSelected) => drawLine(r, line, isSelected),
    drawPoint: (p, color, size, isSelected) => drawPoint(r, p, color, size, isSelected),
  };
  return r;
};

// Everything a resting paint of the editor reads, taken now, so later edits never reach it: the
// base (never drawn into again — a pixel change swaps the image), a copy of the lines, the toggles.
export const restingJob = (app) => ({
  base: app.renderer.restingBase(),
  width: app.canvas.width,
  height: app.canvas.height,
  lines: structuredClone(app.lines),
  showLines: app.showLines,
  showPoints: app.showPoints,
  pointSize: app.pointSize,
});
