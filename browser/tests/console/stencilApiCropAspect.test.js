// crop({ aspect }) (js/console/stencilApi.js) — port of core/tests/cropSpec.test.cpp's
// aspect cases: shrink about the centre, exact fits, fractional centres and the 1px clamp.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp, called, lastCall } from '../helpers/stencilApiRig.js';

// ── crop({ aspect }) — port of core/tests/cropSpec.test.cpp's aspect cases ──────
const makeAspectApp = (cropRect) => makeApp({
  originalImage: {},
  cropRect,
  effectiveOriginalDims: () => ({ width: 4000, height: 4000 }),
  getPageDimensions: () => ({ width: 21, height: 29.7 }),
  canvas: { width: 200, height: 200 },
  defaultCropRect: () => ({ x: 0, y: 0, width: 4000, height: 4000 }),
  applyCrop: function (rect, o) { this.calls.push(['applyCrop', rect, o]); },
});
const eq = (a, b) => Math.abs(a - b) < 1e-9;

test('crop aspect shrinks the width about the centre', () => {
  // Edge rect 200x100 is too wide for 1:1 → width shrinks to 100, centred at x=100.
  const app = makeAspectApp({ x: 0, y: 0, width: 500, height: 500 });
  createStencil(app).crop({ x1: '0px', x2: '200px', y1: '0px', y2: '100px', aspect: '1:1' });
  const [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 50) && eq(r.y, 0) && eq(r.width, 100) && eq(r.height, 100), JSON.stringify(r));
});

test('crop aspect shrinks the height about the centre', () => {
  // The same 200x100 rect is too tall for 4:1 → height shrinks to 50, centred at y=50.
  const app = makeAspectApp({ x: 0, y: 0, width: 500, height: 500 });
  createStencil(app).crop({ x1: '0px', x2: '200px', y1: '0px', y2: '100px', aspect: '4:1' });
  const [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 0) && eq(r.y, 25) && eq(r.width, 200) && eq(r.height, 50), JSON.stringify(r));
});

test('crop aspect that already fits exactly is a no-op', () => {
  const app = makeAspectApp({ x: 0, y: 0, width: 500, height: 500 });
  createStencil(app).crop({ x1: '10px', x2: '210px', y1: '20px', y2: '120px', aspect: '2:1' });
  const [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 10) && eq(r.y, 20) && eq(r.width, 200) && eq(r.height, 100), JSON.stringify(r));
});

test('crop with only aspect applies to the current full rect', () => {
  // No edges → the rect stays the current crop (headless: the full image), then fits 1:1.
  let app = makeAspectApp({ x: 0, y: 0, width: 640, height: 480 });
  createStencil(app).crop({ aspect: '1:1' });
  let [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 80) && eq(r.y, 0) && eq(r.width, 480) && eq(r.height, 480), JSON.stringify(r));

  // Already at the ratio → untouched.
  app = makeAspectApp({ x: 0, y: 0, width: 640, height: 480 });
  createStencil(app).crop({ aspect: '4:3' });
  [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 0) && eq(r.width, 640) && eq(r.height, 480), JSON.stringify(r));
});

test('crop aspect keeps fractional centres exactly (no early rounding)', () => {
  // 101x100 rect, 1:1 → width 100, so x moves by half a pixel: 0.5.
  const app = makeAspectApp({ x: 0, y: 0, width: 500, height: 500 });
  createStencil(app).crop({ x1: '0px', x2: '101px', y1: '0px', y2: '100px', aspect: '1:1' });
  const [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.x, 0.5) && eq(r.width, 100) && eq(r.height, 100), JSON.stringify(r));
});

test('crop aspect degenerate result clamps to 1px, centre kept', () => {
  const app = makeAspectApp({ x: 0, y: 0, width: 500, height: 500 });
  createStencil(app).crop({ x1: '0px', x2: '100px', y1: '0px', y2: '100px', aspect: '1000:1' });
  const [, r] = lastCall(app, 'applyCrop');
  assert.ok(eq(r.width, 100), JSON.stringify(r));    // never grows past the resolved rect
  assert.ok(eq(r.height, 1), JSON.stringify(r));     // 0.1px floored to 1px
  assert.ok(eq(r.y, 49.5) && eq(r.x, 0), JSON.stringify(r));  // centre preserved
});

test('crop aspect invalid strings throw before any applyCrop commit', () => {
  for (const bad of ['0:3', '4:0', '-4:3', '4:-3', '4', '4:', ':3', '4:3:2',
                     'a:b', '4.5:3', '1e2:3', '']) {
    const app = makeAspectApp({ x: 0, y: 0, width: 640, height: 480 });
    assert.throws(() => createStencil(app).crop({ aspect: bad }),
                  /crop aspect must be "W:H" with positive integers/, JSON.stringify(bad));
    assert.equal(called(app, 'applyCrop').length, 0);
  }
});
