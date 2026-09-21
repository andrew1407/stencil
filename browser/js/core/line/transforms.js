// Pure point-list geometry: bbox centre, rotate, mirror. Each is the shared C++ core (wasm)
// op with the JS body as the reference + fallback — a wasm-parity surface, keep both identical.
import { core } from '../abi/stencilCore.js';

export const bboxCenterOf = (pts) => {
  const bboxCenter = core.op('boundingBoxCenter');
  if (bboxCenter) return bboxCenter(pts);
  let minX = Infinity; let minY = Infinity; let maxX = -Infinity; let maxY = -Infinity;
  for (const p of pts) {
    if (p.x < minX) minX = p.x;
    if (p.x > maxX) maxX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.y > maxY) maxY = p.y;
  }
  return { x: (minX + maxX) / 2, y: (minY + maxY) / 2 };
};

export const rotatePointsAbout = (pts, cx, cy, angle) => {
  const rotate = core.op('rotatePoints');
  if (rotate) { rotate(pts, cx, cy, angle); return; }
  const cos = Math.cos(angle);
  const sin = Math.sin(angle);
  pts.forEach((p) => {
    const dx = p.x - cx;
    const dy = p.y - cy;
    p.x = cx + dx * cos - dy * sin;
    p.y = cy + dx * sin + dy * cos;
  });
};

// Horizontal flips x' = 2cx - x, vertical y' = 2cy - y.
export const flipPointsAbout = (pts, horizontal, cx, cy) => {
  const flip = core.op('flipPoints');
  if (flip) { flip(pts, horizontal, cx, cy); return; }
  pts.forEach((p) => {
    if (horizontal) p.x = 2 * cx - p.x;
    else p.y = 2 * cy - p.y;
  });
};

// Would a point at (x, y) land on the stroke's first point, closing it?
export const shouldCloseShape = (points, x, y, pointSize) => {
  const fn = core.op('shouldCloseShape');
  if (fn) return fn(points, { x, y }, pointSize);
  if (points.length < 3) return false;
  const p0 = points[0];
  return Math.hypot(p0.x - x, p0.y - y) <= pointSize + 8;
};
