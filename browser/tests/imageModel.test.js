// Unit tests for ImageModel (js/core/imageModel.js) — the crop/rotation geometry extracted
// out of DrawingApp. The pure bits (roundRect, rotatedOriginalDims, defaultCropRect) need only
// a fake app; the canvas-touching bits (rebuildCroppedImage via rotateImage/applyCrop) run
// against a minimal document.createElement('canvas') stub so we can assert the crop/rotation
// bookkeeping (rotationQuarters wrap, orientation-flip line clearing) without real rendering.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// notify() spy (utils.js posts to #notify-balloon).
const notifications = [];
const balloon = { notify: (m, t) => notifications.push([m, t]) };

// Fake canvas: records size, hands back a no-op 2D context.
const makeCanvas = () => ({
  width: 0, height: 0,
  getContext: () => ({ translate() {}, rotate() {}, drawImage() {} }),
  toDataURL: () => 'data:fake',
});
globalThis.document = {
  getElementById: (id) => (id === 'notify-balloon' ? balloon : null),
  createElement: (tag) => (tag === 'canvas' ? makeCanvas() : {}),
};

const { ImageModel } = await import('../js/core/imageModel.js');

const makeApp = (over = {}) => {
  const rec = { save: 0, redraw: 0, remoteSync: 0 };
  return {
    rec,
    originalImage: { width: 200, height: 100 },
    rotationQuarters: 0,
    cropRect: { x: 0, y: 0, width: 200, height: 100 },
    pageSize: 'A4', customPageWidth: 21, customPageHeight: 29.7,
    lines: [], currentLine: null, selectedLineIdx: -1, coordLineIdx: -1, focusedPtIdx: -1,
    canvas: { width: 0, height: 0 },
    image: null, imageDataUrl: 'data:orig',
    history: { reset() {} },
    zoomPan: { fitToWindow() {} },
    renderer: { redraw() { rec.redraw++; } },
    storage: { save() { rec.save++; } },
    coordTable: { update() {} },
    hideSelectionPanels() {},
    updateInfo() {}, updateButtons() {}, updateCoordStatus() {},
    remoteSync: { scheduleRemoteSync() { rec.remoteSync++; } },
    ...over,
  };
};

const reset = () => { notifications.length = 0; };

test('roundRect: rounds + clamps inside the rotated original', () => {
  const m = new ImageModel(makeApp());
  // Oversized rect clamps to the image bounds (200x100).
  assert.deepEqual(m.roundRect({ x: -5, y: -5, width: 999, height: 999 }), { x: 0, y: 0, width: 200, height: 100 });
  // Sub-pixel values round; x clamps so the rect stays inside (x ≤ iw - width).
  assert.deepEqual(m.roundRect({ x: 190.4, y: 2.6, width: 20.5, height: 10.2 }), { x: 179, y: 3, width: 21, height: 10 });
});

test('rotatedOriginalDims: swaps w/h on odd quarter-turns only', () => {
  const app = makeApp();
  const m = new ImageModel(app);
  assert.deepEqual(m.rotatedOriginalDims(), { w: 200, h: 100 });
  app.rotationQuarters = 1;
  assert.deepEqual(m.rotatedOriginalDims(), { w: 100, h: 200 });
  app.rotationQuarters = 2;
  assert.deepEqual(m.rotatedOriginalDims(), { w: 200, h: 100 });
});

test('defaultCropRect: returns an integer rect within the image bounds', () => {
  const r = new ImageModel(makeApp()).defaultCropRect();
  assert.ok(Number.isInteger(r.x) && Number.isInteger(r.width));
  assert.ok(r.x >= 0 && r.y >= 0 && r.width <= 200 && r.height <= 100);
});

test('rotateImage: no image → fail notify, no mutation', () => {
  reset();
  const app = makeApp({ originalImage: null });
  new ImageModel(app).rotateImage(1);
  assert.deepEqual(notifications.at(-1), ['Open an image first', 'fail']);
  assert.equal(app.rotationQuarters, 0);
});

test('rotateImage: CW increments the quarter-turn count and refreshes', () => {
  const app = makeApp();
  const m = new ImageModel(app);
  m.rotateImage(1);
  assert.equal(app.rotationQuarters, 1);
  m.rotateImage(1); m.rotateImage(1); m.rotateImage(1);
  assert.equal(app.rotationQuarters, 0);   // 4 CW turns wrap to 0
  assert.equal(app.rec.save > 0, true);
  assert.equal(app.rec.remoteSync > 0, true);
});

test('rotateImage: CCW wraps below zero to 3', () => {
  const app = makeApp();
  new ImageModel(app).rotateImage(-1);
  assert.equal(app.rotationQuarters, 3);
});

test('applyCrop recalc: an orientation flip clears the lines', () => {
  const app = makeApp({ lines: [{ points: [{ x: 1, y: 1 }] }] });
  // Original crop is landscape (200x100); apply a portrait rect → orientationChanged → clear.
  new ImageModel(app).applyCrop({ x: 0, y: 0, width: 80, height: 100 }, { recalc: true });
  assert.equal(app.lines.length, 0);
  assert.deepEqual(app.cropRect, { x: 0, y: 0, width: 80, height: 100 });
});
