// stencil.zoom and stencil.crop (js/console/stencilApi.js): the recentring steps, the rect
// commit through applyCrop, the derived axis and the scale form.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp, called, lastCall } from '../helpers/stencilApiRig.js';

// ── Zoom / crop ─────────────────────────────────────────────────────────────────
test('zoom: relative step recentres; passing a point keeps it fixed; zoomLevel get/set', () => {
  const app = makeApp({ scale: 1 });
  const stencil = createStencil(app);

  stencil.zoom(0.25);
  assert.deepEqual(lastCall(app, 'zoomAroundCenter'), ['zoomAroundCenter', 1.25]);
  stencil.zoom(0.5, { x: 10, y: 20 });
  assert.deepEqual(lastCall(app, 'zoomToImagePoint'), ['zoomToImagePoint', 1.5, 10, 20]);

  assert.equal(stencil.zoomLevel, 100);
  stencil.zoomLevel = 150;
  assert.deepEqual(lastCall(app, 'zoomAroundCenter'), ['zoomAroundCenter', 1.5]);
});

test('crop throws without an image, and otherwise commits a rect via applyCrop', () => {
  const app = makeApp();
  const stencil = createStencil(app);
  assert.throws(() => stencil.crop({ x1: 10 }), /No image loaded/);

  // Loaded image: provide the geometry crop() reads, assert applyCrop gets a numeric rect.
  const app2 = makeApp({
    originalImage: {},
    cropRect: { x: 0, y: 0, width: 100, height: 100 },
    effectiveOriginalDims: () => ({ width: 200, height: 200 }),
    getPageDimensions: () => ({ width: 21, height: 29.7 }),
    canvas: { width: 200, height: 200 },
    defaultCropRect: () => ({ x: 0, y: 0, width: 200, height: 200 }),
    applyCrop: function (rect, opts) { this.calls.push(['applyCrop', rect, opts]); },
  });
  const stencil2 = createStencil(app2);
  assert.equal(stencil2.crop({ x1: 10, x2: 90 }), stencil2);
  const [, rect, opts] = lastCall(app2, 'applyCrop');
  for (const k of ['x', 'y', 'width', 'height']) assert.equal(typeof rect[k], 'number');
  assert.deepEqual(opts, { recalc: true });
});

test('crop derives the missing axis from the page proportion when only one axis is given', () => {
  const makeCropApp = () => makeApp({
    originalImage: {},
    cropRect: { x: 5, y: 7, width: 100, height: 100 },
    effectiveOriginalDims: () => ({ width: 2000, height: 2000 }),
    getPageDimensions: () => ({ width: 21, height: 29.7 }),   // A4 proportions
    canvas: { width: 200, height: 200 },
    defaultCropRect: () => ({ x: 0, y: 0, width: 200, height: 200 }),
    applyCrop: function (rect, o) { this.calls.push(['applyCrop', rect, o]); },
  });
  const near = (a, b) => Math.abs(a - b) < 0.5;

  // Only the x axis is given, so height = width × (29.7 / 21) for portrait and the unspecified y
  // keeps the current top edge; 'px' tokens are absolute, a bare number is a delta.
  let app = makeCropApp();
  createStencil(app).crop({ x1: '10px', x2: '90px' });   // width 80
  let [, rect] = lastCall(app, 'applyCrop');
  assert.ok(near(rect.width, 80) && near(rect.height, 80 * 29.7 / 21), `portrait x→y ${JSON.stringify(rect)}`);
  assert.equal(rect.y, 7);

  // album true (landscape) inverts the relation: height = width × (21 / 29.7).
  app = makeCropApp();
  createStencil(app).crop({ x1: '10px', x2: '90px', album: true });
  [, rect] = lastCall(app, 'applyCrop');
  assert.ok(near(rect.height, 80 * 21 / 29.7), `landscape x→y ${JSON.stringify(rect)}`);

  // Only the y axis (and only one edge of it) → width is derived, x keeps its start.
  app = makeCropApp();
  createStencil(app).crop({ y2: '207px' });      // height = 207 - currentTop(7) = 200
  [, rect] = lastCall(app, 'applyCrop');
  assert.ok(near(rect.height, 200) && near(rect.width, 200 * 21 / 29.7), `portrait y→x ${JSON.stringify(rect)}`);
  assert.equal(rect.x, 5);

  // Both axes given → free-form, no proportion adjustment.
  app = makeCropApp();
  createStencil(app).crop({ x1: '0px', x2: '100px', y1: '0px', y2: '40px' });
  [, rect] = lastCall(app, 'applyCrop');
  assert.ok(near(rect.width, 100) && near(rect.height, 40), `free-form ${JSON.stringify(rect)}`);
});

test('crop({ scale }) scales the rect about its centre via applyCrop; rejects non-positive', () => {
  const makeScaleApp = () => makeApp({
    originalImage: {},
    cropRect: { x: 60, y: 60, width: 80, height: 80 },   // centre (100,100) in a 200x200 image
    effectiveOriginalDims: () => ({ width: 200, height: 200 }),
    applyCrop: function (rect, o) { this.calls.push(['applyCrop', rect, o]); },
  });

  // Grow 1.5×: 80 → 120, centre held at (100,100), committed with recalc (chainable).
  let app = makeScaleApp();
  const stencil = createStencil(app);
  assert.equal(stencil.crop({ scale: 1.5 }), stencil);
  let [, rect, opts] = lastCall(app, 'applyCrop');
  assert.ok(Math.abs(rect.width - 120) < 1e-6 && Math.abs(rect.height - 120) < 1e-6, JSON.stringify(rect));
  assert.ok(Math.abs(rect.x + rect.width / 2 - 100) < 1e-6 && Math.abs(rect.y + rect.height / 2 - 100) < 1e-6);
  assert.deepEqual(opts, { recalc: true });

  // Shrink 0.5×: 80 → 40.
  app = makeScaleApp();
  createStencil(app).crop({ scale: 0.5 });
  [, rect] = lastCall(app, 'applyCrop');
  assert.ok(Math.abs(rect.width - 40) < 1e-6, JSON.stringify(rect));

  // Non-positive / non-numeric scale throws BEFORE any applyCrop commit.
  for (const bad of [0, -2, 'not-a-number']) {
    const a = makeScaleApp();
    assert.throws(() => createStencil(a).crop({ scale: bad }), /crop scale must be a positive number/);
    assert.equal(called(a, 'applyCrop').length, 0);
  }
});
