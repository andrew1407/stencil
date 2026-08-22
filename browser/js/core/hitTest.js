import { distToSegment } from '../utils.js';

// ── Pure hit-testing over the editor's line model ─────────────────────
// Extracted from drawingApp.js; DrawingApp keeps thin delegators that supply its
// lines/currentLine and the zoom-aware default thresholds (base screen-px radius divided
// by the zoom, so hits stay constant ON SCREEN at any zoom).

// Index of the topmost line within `threshold` of (x, y) — by point (padded +4) or by
// segment — or -1.
export function findLineAt(lines, x, y, threshold) {
  for (let i = lines.length - 1; i >= 0; i--) {
    const pts = lines[i].points;

    for (const p of pts)
      if (Math.hypot(p.x - x, p.y - y) <= threshold + 4) return i;

    for (let j = 0; j < pts.length - 1; j++)
      if (distToSegment(x, y, pts[j], pts[j + 1]) <= threshold) return i;
  }
  return -1;
}

// First point within `threshold` of (x, y) across committed lines then the in-progress
// line, or null.
export function findNearestPoint(lines, currentLine, x, y, threshold) {
  const allPoints = [];

  lines.forEach(line => {
    line.points.forEach(p => allPoints.push(p));
  });

  if (currentLine) currentLine.points.forEach(p => allPoints.push(p));

  for (let point of allPoints) {
    const dist = Math.sqrt((point.x - x) ** 2 + (point.y - y) ** 2);
    if (dist < threshold) return point;
  }
  return null;
}

// Nearest point as { lineIdx, ptIdx, point } (lineIdx -1 = the in-progress line, which is
// checked first; committed lines are scanned topmost-first), or null.
export function findNearestPointWithIdx(lines, currentLine, x, y, threshold) {
  if (currentLine) {
    for (let i = 0; i < currentLine.points.length; i++) {
      const p = currentLine.points[i];
      if (Math.hypot(p.x - x, p.y - y) < threshold) return { lineIdx: -1, ptIdx: i, point: p };
    }
  }
  for (let li = lines.length - 1; li >= 0; li--) {
    for (let pi = 0; pi < lines[li].points.length; pi++) {
      const p = lines[li].points[pi];
      if (Math.hypot(p.x - x, p.y - y) < threshold) return { lineIdx: li, ptIdx: pi, point: p };
    }
  }
  return null;
}

// Nearest segment among completed lines as { lineIdx, ptIdx1, ptIdx2 }, or null.
export function findNearestSegmentWithIdx(lines, x, y, threshold) {
  let bestDist = Infinity;
  let best = null;
  for (let li = lines.length - 1; li >= 0; li--) {
    const pts = lines[li].points;
    for (let pi = 0; pi < pts.length - 1; pi++) {
      const d = distToSegment(x, y, pts[pi], pts[pi + 1]);
      if (d < threshold && d < bestDist) {
        bestDist = d;
        best = { lineIdx: li, ptIdx1: pi, ptIdx2: pi + 1 };
      }
    }
  }
  return best;
}
