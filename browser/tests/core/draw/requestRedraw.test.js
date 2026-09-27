// A drag's burst of moves must paint once per frame: requestRedraw coalesces, redraw stays
// synchronous, and the point and divider drags, the nudge/rotate/flip bursts and the hold preview
// go through the coalescer.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Renderer } from '../../../js/core/draw/renderer.js';
import { movePointTo } from '../../../js/core/touch/dragGestures.js';

const withFrames = (t) => {
  const queue = [];
  const saved = globalThis.requestAnimationFrame;
  globalThis.requestAnimationFrame = (cb) => queue.push(cb);
  t.after(() => { globalThis.requestAnimationFrame = saved; });
  return () => { const q = queue.splice(0); for (const cb of q) cb(16); return q.length; };
};

test('a burst of requestRedraw calls paints once, on the next frame', (t) => {
  const flush = withFrames(t);
  const r = new Renderer({ image: null });
  let paints = 0;
  r.redraw = () => { paints++; };
  for (let i = 0; i < 20; i++) r.requestRedraw();
  assert.equal(paints, 0, 'nothing paints inside the event');
  assert.equal(flush(), 1, 'one frame was asked for');
  assert.equal(paints, 1);
  r.requestRedraw();
  flush();
  assert.equal(paints, 2, 'the next burst gets its own frame');
});

test('a point drag moves the point now and paints on the frame', (t) => {
  const flush = withFrames(t);
  const line = { points: [{ x: 0, y: 0 }] };
  const renderer = new Renderer({ image: null });
  let paints = 0;
  renderer.redraw = () => { paints++; };
  const app = { lines: [line], renderer, coordTable: { refreshCoordRow() {} } };
  for (let i = 1; i <= 5; i++) movePointTo(app, { lineIdx: 0, ptIdx: 0 }, i, i);
  assert.deepEqual(line.points[0], { x: 5, y: 5 });
  flush();
  assert.equal(paints, 1);
});

test('a divider drag paints on the frame; a one-shot split paints at once', async (t) => {
  const flush = withFrames(t);
  const { SettingsController } = await import('../../../js/core/settings/controller.js');
  const renderer = new Renderer({ image: null });
  let paints = 0;
  renderer.redraw = () => { paints++; };
  const settings = new SettingsController({ renderer });
  for (const f of [0.2, 0.3, 0.4]) settings.setCompareSplit(f, { dragging: true });
  assert.equal(settings.app.compareSplit, 0.4);
  assert.equal(paints, 0);
  flush();
  assert.equal(paints, 1);
  settings.setCompareSplit(0.5);
  assert.equal(paints, 2, 'the console facade still sees its pixels at once');
});

test('a burst of nudges or rotates paints once and refreshes the points table once per frame', async (t) => {
  const flush = withFrames(t);
  const { nudgeSelected, rotateSelectedLine } = await import('../../../js/core/draw/transformOps.js');
  const renderer = new Renderer({ image: null });
  let paints = 0, tables = 0;
  renderer.redraw = () => { paints++; };
  const app = {
    lines: [{ points: [{ x: 0, y: 0 }, { x: 10, y: 0 }] }], selectedLines: [], selectedLineIdx: 0, coordLineIdx: 0,
    focusedPtIdx: -1, renderer, coordTable: { update(pts, i) { tables++; assert.equal(i, 0); } },
    saveHistory() {}, storage: { saveSoon() {} },
  };
  t.after(() => clearTimeout(app.rotateSaveTimer));
  for (let i = 0; i < 8; i++) nudgeSelected(app, 1, 0);
  assert.deepEqual(app.lines[0].points[0], { x: 8, y: 0 }, 'the points move inside the event');
  assert.deepEqual([paints, tables], [0, 0], 'nothing paints inside the burst');
  assert.equal(flush(), 2, 'one frame for the stage, one for the table');
  assert.deepEqual([paints, tables], [1, 1]);
  for (let i = 0; i < 5; i++) rotateSelectedLine(app, Math.PI / 60);
  app.coordLineIdx = -1;
  flush();
  assert.deepEqual([paints, tables], [2, 1], 'a table that moved on to another line is left alone');
});

test('a burst of hold-preview moves paints once per frame; clearing it paints at once', async (t) => {
  const flush = withFrames(t);
  const { setHoldPreview, clearHoldPreview } = await import('../../../js/core/draw/holdDrawView.js');
  const renderer = new Renderer({ image: null });
  let paints = 0;
  renderer.redraw = () => { paints++; };
  const app = { renderer, holdPreview: null };
  for (let i = 0; i < 10; i++) setHoldPreview(app, i, i);
  assert.deepEqual([paints, app.holdPreview], [0, { x: 9, y: 9 }]);
  flush();
  assert.equal(paints, 1);
  clearHoldPreview(app);
  assert.deepEqual([paints, app.holdPreview], [2, null], 'the gesture\'s end is not left for a frame');
});
