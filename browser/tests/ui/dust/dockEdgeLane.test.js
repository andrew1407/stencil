// js/ui/motion/surface/motion.js dockEdgeLane: a surface docked flush on a window edge streams its grains
// out THROUGH that edge, each on its own row, instead of at a point 1.2 widths past it — which a panel
// flush on the edge crossed in two frames, a still rect and then nothing (user report).
import test from 'node:test';
import assert from 'node:assert';
import { dockEdgeLane, dockAwayPoint, surfaceMotion, SURFACE_SPREAD } from '../../../js/ui/motion.js';

const rect = (left, top, width, height) => ({ left, top, width, height });
const withViewport = (w, h, fn) => {
  const had = [globalThis.innerWidth, globalThis.innerHeight];
  globalThis.innerWidth = w;
  globalThis.innerHeight = h;
  try { fn(); } finally { [globalThis.innerWidth, globalThis.innerHeight] = had; }
};

test('each dock leaves through its own window edge, just past it, as a lane', () => {
  withViewport(1400, 900, () => {
    const panel = rect(1000, 400, 380, 460);
    assert.deepStrictEqual(dockEdgeLane(panel, 'right'), { x: 1464, y: 630, lane: 'x' });
    assert.deepStrictEqual(dockEdgeLane(rect(0, 0, 340, 900), 'left'), { x: -64, y: 450, lane: 'x' });
    assert.deepStrictEqual(dockEdgeLane(rect(20, 60, 1360, 217), 'top'), { x: 700, y: -64, lane: 'y' });
    assert.deepStrictEqual(dockEdgeLane(rect(20, 800, 1360, 80), 'bottom'), { x: 700, y: 964, lane: 'y' });
  });
});

test('with no viewport to read it falls back to the dock-away point', () => {
  withViewport(undefined, undefined, () => {
    const panel = rect(1000, 400, 380, 460);
    assert.deepStrictEqual(dockEdgeLane(panel, 'right'), dockAwayPoint(panel, 'right'));
  });
});

test('on a lane every cell crosses straight to the edge on its own row, never to one point', () => {
  const box = rect(1000, 400, 380, 460);
  const lane = { x: 1464, y: 630, lane: 'x' };
  const cols = 10, rows = 12;
  const ends = [];
  for (let cy = 0; cy < rows; cy++) {
    const s = surfaceMotion(4, cy, cols, rows, box, lane);
    const homeX = box.left + 4.5 * (box.width / cols);
    const homeY = box.top + (cy + 0.5) * (box.height / rows);
    assert.ok(Math.abs(s.dy) <= SURFACE_SPREAD / 2 + 1, `row ${cy} keeps its height (dy ${s.dy})`);
    assert.ok(Math.abs(homeX + s.dx - lane.x) <= SURFACE_SPREAD / 2 + 1, `row ${cy} reaches the edge`);
    ends.push(homeY + s.dy);
  }
  const spanY = Math.max(...ends) - Math.min(...ends);
  assert.ok(spanY > box.height * 0.8, `the rows stay spread over the panel's height (${spanY}px), no blob`);
  // A point target, by contrast, pulls every row to one height.
  const point = surfaceMotion(4, 0, cols, rows, box, { x: 1464, y: 630 });
  assert.ok(Math.abs(box.top + 0.5 * (box.height / rows) + point.dy - 630) <= SURFACE_SPREAD / 2 + 1);
});
