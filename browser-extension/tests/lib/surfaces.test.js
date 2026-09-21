// Surfaces made of dust (src/lib/motion.js surfaceIn / surfaceOut) — the extension half of the
// shared contract. This half pins the geometry: the grid, one mote's flight, the control's centre.
import test from 'node:test';
import assert from 'node:assert';
import { motionSrc } from '../helpers/sources.js';

import {
  reshapeGrid, surfaceMotion, centerOf,
  SURFACE_COLS, SURFACE_ROWS, SURFACE_MOTE_PX, SURFACE_SPREAD, SURFACE_OUT_MS,
} from '../../src/lib/motion.js';

// Motes are sized in PIXELS: a fixed grid over a wide box gives slivers, over a small one real
// dust. The quoted grid is only the frame-budget ceiling.
test('reshapeGrid aims for the mote size, not a fixed cell count', () => {
  const { cols, rows } = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, 160, 240, SURFACE_MOTE_PX);
  assert.equal(cols, 27);   // 160 / 6
  assert.equal(rows, 40);   // 240 / 6
});

test('reshapeGrid holds a full-window surface to the quoted budget', () => {
  const budget = SURFACE_COLS * SURFACE_ROWS;
  const { cols, rows } = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, 1040, 760, SURFACE_MOTE_PX);
  // Both axes are scaled by the same factor and then rounded, so the product can land a couple of
  // percent over the budget — never an order of it.
  assert.ok(cols * rows <= budget * 1.05, `1040x760 comes back near the budget (got ${cols}x${rows})`);
  assert.ok(cols * rows >= budget * 0.8, 'and it actually spends the budget it has');
  assert.ok(cols >= 3 && rows >= 3);
});

test('reshapeGrid keeps three bands each way — two reads as splitting in half', () => {
  const tiny = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, 9, 9, SURFACE_MOTE_PX);
  assert.deepEqual(tiny, { cols: 3, rows: 3 });
});

// ── One mote's flight ───────────────────────────────────────────────────────
const BOX = { left: 100, top: 100, width: 200, height: 120 };
const POINT = { x: 400, y: 60 };     // an icon up and to the right of the box

test('every mote is aimed at the point that owns the surface', () => {
  // The cell centre plus the mote's own (dx, dy) lands on the point, up to the fan.
  for (const [cx, cy] of [[0, 0], [9, 5], [19, 11]]) {
    const m = surfaceMotion(cx, cy, 20, 12, BOX, POINT);
    const mx = BOX.left + ((cx + 0.5) * BOX.width) / 20;
    const my = BOX.top + ((cy + 0.5) * BOX.height) / 12;
    assert.ok(Math.abs(mx + m.dx - POINT.x) <= SURFACE_SPREAD / 2 + 1, 'lands on the point in x');
    assert.ok(Math.abs(my + m.dy - POINT.y) <= SURFACE_SPREAD / 2 + 1, 'lands on the point in y');
  }
});

test('the sweep rides the DISTANCE: the near edge goes first, the far one last', () => {
  // Column 19 is the edge facing the point; column 0 is the far one.
  const near = surfaceMotion(19, 0, 20, 12, BOX, POINT).delay;
  const far = surfaceMotion(0, 11, 20, 12, BOX, POINT).delay;
  assert.ok(near < far, `near ${near}ms leads far ${far}ms`);
});

test('a mote never starts after the flight it belongs to', () => {
  for (let cy = 0; cy < 12; cy++) {
    for (let cx = 0; cx < 20; cx++) {
      const { delay } = surfaceMotion(cx, cy, 20, 12, BOX, POINT, { span: SURFACE_OUT_MS });
      assert.ok(delay >= 0 && delay < SURFACE_OUT_MS, `delay ${delay} inside the span`);
    }
  }
});

test('surfaceMotion is deterministic — the same cell always flies the same way', () => {
  assert.deepEqual(surfaceMotion(3, 4, 20, 12, BOX, POINT), surfaceMotion(3, 4, 20, 12, BOX, POINT));
  assert.notDeepEqual(surfaceMotion(3, 4, 20, 12, BOX, POINT), surfaceMotion(4, 3, 20, 12, BOX, POINT));
});

test('a degenerate box or point never produces NaN', () => {
  for (const m of [surfaceMotion(0, 0, 0, 0, null, null), surfaceMotion(0, 0, 1, 1, BOX, {})]) {
    for (const v of Object.values(m)) assert.ok(Number.isFinite(v), 'every value is a number');
  }
});

// ── What a surface's cloud is made of ───────────────────────────────────────
test('a surface never dusts as copies of ITSELF — a cloud carries no identity', () => {
  // A surface's cloud lands on <body>, so it paints specks rather than cloning: a few hundred copies
  // of a menu would answer to `.action-menu`, an id, and every query on the page.
  const motionJs = motionSrc();
  const dust = motionJs.slice(motionJs.indexOf('const surfaceDust ='));
  assert.match(dust, /paintTile: speckPainter\(el\),/, 'a surface always paints specks');
  assert.ok(!/makeCopy|cloneNode|cloneForTile/.test(dust.slice(0, dust.indexOf('settleSurface'))),
    'and never hands disintegrate an element to copy');
});

test('centerOf is the middle of the control, and null when there is nothing to measure', () => {
  assert.deepEqual(centerOf({ left: 10, top: 20, width: 30, height: 40 }), { x: 25, y: 40 });
  assert.deepEqual(centerOf({ getBoundingClientRect: () => ({ left: 0, top: 0, width: 8, height: 4 }) }),
    { x: 4, y: 2 });
  assert.equal(centerOf(null), null);
  assert.equal(centerOf({}), null);
});
