// Unit tests for InputController (js/core/inputController.js) — the touch + hold-to-draw input
// layer extracted out of DrawingApp. The gesture wiring needs real pointer/touch events (covered
// by driving the real browser app), so here we pin the two DOM-free public methods: the
// hold-delay clamp/persist and the preview anchor-point resolution.

import { test } from 'node:test';
import assert from 'node:assert/strict';

const { InputController } = await import('../js/core/inputController.js');

const makeApp = (over = {}) => {
  const rec = { save: 0 };
  return {
    rec,
    holdDrawDelay: 500,
    currentLine: null,
    lines: [],
    continueLineIdx: -1,
    continueInsertIdx: 0,
    storage: { save() { rec.save++; } },
    ...over,
  };
};

test('setHoldDrawDelay: clamps to [100,3000], rounds, and persists', () => {
  const app = makeApp();
  const ic = new InputController(app);
  ic.setHoldDrawDelay(750.6);
  assert.equal(app.holdDrawDelay, 751);
  assert.equal(app.rec.save, 1);
  ic.setHoldDrawDelay(5000);
  assert.equal(app.holdDrawDelay, 3000);   // clamped high
  ic.setHoldDrawDelay(10);
  assert.equal(app.holdDrawDelay, 100);    // clamped low
});

test('setHoldDrawDelay: non-numeric is ignored; persist:false skips the write', () => {
  const app = makeApp();
  const ic = new InputController(app);
  ic.setHoldDrawDelay('abc');
  assert.equal(app.holdDrawDelay, 500);    // unchanged
  ic.setHoldDrawDelay(600, { persist: false });
  assert.equal(app.holdDrawDelay, 600);
  assert.equal(app.rec.save, 0);
});

test('holdAnchorPoint: none when there is no in-progress or continued line', () => {
  assert.equal(new InputController(makeApp()).holdAnchorPoint(), null);
});

test('holdAnchorPoint: the last point of the in-progress line', () => {
  const app = makeApp({ currentLine: { points: [{ x: 1, y: 1 }, { x: 2, y: 2 }] } });
  assert.deepEqual(new InputController(app).holdAnchorPoint(), { x: 2, y: 2 });
});

test('holdAnchorPoint: forward-extend anchors to the point before the insert tail', () => {
  const app = makeApp({
    currentLine: null,
    continueLineIdx: 0,
    continueInsertIdx: 2,
    lines: [{ points: [{ x: 0, y: 0 }, { x: 5, y: 5 }, { x: 9, y: 9 }] }],
  });
  // forward mode (default, #holdPrepend=false) → pts[continueInsertIdx-1] = pts[1]
  assert.deepEqual(new InputController(app).holdAnchorPoint(), { x: 5, y: 5 });
});
