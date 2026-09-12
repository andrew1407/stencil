import { distToSegment } from '../utils.js';

// Pure hit-testing over the line model; DrawingApp supplies lines/currentLine and the
// zoom-aware thresholds (screen-px radius / zoom, so hits stay constant on screen).
// Every scan runs 2-3x per mouse-move over every point, so each is allocation-free and
// takes the same bbox early reject as core/geometry/hitTest.cpp findLineAt.
const farFromBox = (pts, x, y, margin) => {
  let minX = pts[0].x, maxX = minX, minY = pts[0].y, maxY = minY;
  for (let i = 1; i < pts.length; i++) {
    const p = pts[i];
    if (p.x < minX) minX = p.x; else if (p.x > maxX) maxX = p.x;
    if (p.y < minY) minY = p.y; else if (p.y > maxY) maxY = p.y;
  }
  return x < minX - margin || x > maxX + margin || y < minY - margin || y > maxY + margin;
};

// Topmost line within `threshold` — by point (padded +4) or by segment — or -1.
export function findLineAt(lines, x, y, threshold) {
  const margin = threshold + 4;
  for (let i = lines.length - 1; i >= 0; i--) {
    const pts = lines[i].points;
    if (!pts.length || farFromBox(pts, x, y, margin)) continue;

    for (const p of pts)
      if (Math.hypot(p.x - x, p.y - y) <= margin) return i;

    for (let j = 0; j < pts.length - 1; j++)
      if (distToSegment(x, y, pts[j], pts[j + 1]) <= threshold) return i;
  }
  return -1;
}

// Committed lines first, then the in-progress line; or null.
export function findNearestPoint(lines, currentLine, x, y, threshold) {
  const near = (pts) => {
    if (!pts.length || farFromBox(pts, x, y, threshold)) return null;
    for (const point of pts) {
      const dist = Math.sqrt((point.x - x) ** 2 + (point.y - y) ** 2);
      if (dist < threshold) return point;
    }
    return null;
  };
  for (const line of lines) {
    const hit = near(line.points);
    if (hit) return hit;
  }
  return currentLine ? near(currentLine.points) : null;
}

// { lineIdx, ptIdx, point }; lineIdx -1 = the in-progress line, checked first, then topmost-first.
export function findNearestPointWithIdx(lines, currentLine, x, y, threshold) {
  const near = (pts, lineIdx) => {
    if (!pts.length || farFromBox(pts, x, y, threshold)) return null;
    for (let i = 0; i < pts.length; i++) {
      const p = pts[i];
      if (Math.hypot(p.x - x, p.y - y) < threshold) return { lineIdx, ptIdx: i, point: p };
    }
    return null;
  };
  if (currentLine) {
    const hit = near(currentLine.points, -1);
    if (hit) return hit;
  }
  for (let li = lines.length - 1; li >= 0; li--) {
    const hit = near(lines[li].points, li);
    if (hit) return hit;
  }
  return null;
}

// { lineIdx, ptIdx1, ptIdx2 } among completed lines, or null.
export function findNearestSegmentWithIdx(lines, x, y, threshold) {
  let bestDist = Infinity;
  let best = null;
  for (let li = lines.length - 1; li >= 0; li--) {
    const pts = lines[li].points;
    if (!pts.length || farFromBox(pts, x, y, threshold)) continue;
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
