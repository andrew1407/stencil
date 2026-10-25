// The overlay's marks bitmap (js/core/draw/marksCache.js): a frame whose marks did not change blits
// it, any value the paint reads repaints them, a stroke in flight never caches, and a drag (a key
// that moves every frame) paints straight onto the overlay with no extra blit.
import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { recordingCtx, argsOf } from '../../helpers/recordingCtx.js';

let made;
class RecordingOffscreen {
  constructor(width, height) { this.width = width; this.height = height; this.rec = recordingCtx(this); made.push(this); }
  getContext() { return this.rec.ctx; }
}
beforeEach(() => { made = []; globalThis.OffscreenCanvas = RecordingOffscreen; });
afterEach(() => { delete globalThis.OffscreenCanvas; });

const { Renderer } = await import('../../../js/core/draw/renderer.js');

const RESTING = { active: false, pointsOf: (l) => l.points, scaleAt: () => 1, paintUnder() {}, paintOver() {} };
const stage = () => {
  const app = {
    image: null, imageFilter: 'none', canvas: { width: 20, height: 10 }, scale: 1,
    lines: [{ points: [{ x: 2, y: 2 }, { x: 8, y: 6 }], color: '#ff0000', thickness: 2, style: 'solid' }],
    showLines: true, showPoints: true, pointSize: 3, strokeFx: { ...RESTING }, selectedLines: [], selectedLineIdx: -1,
    coordLineIdx: -1, compareMode: 'none', selGlowColor: '#00f', hoverRingColor: '#0f0', focusRingColor: '#f0f',
  };
  app.ctx = recordingCtx(app.canvas).ctx;
  const overlay = { width: 0, height: 0 };
  const top = recordingCtx(overlay);
  overlay.getContext = () => top.ctx;
  const r = new Renderer(app);
  r.useOverlay(overlay);
  app.image = { width: 20, height: 10 };   // after attaching, so no frame is requested before the test's own
  const frame = () => { top.calls.length = 0; r.redraw(); return top.calls; };
  return { app, frame };
};
const arcs = (calls) => argsOf(calls, 'arc').length;
const blits = (calls) => argsOf(calls, 'drawImage').length;

test('a steady frame blits the marks it painted once into the bitmap', () => {
  const { frame } = stage();
  assert.equal(arcs(frame()), 2, 'a new key paints straight onto the overlay');
  const second = frame();
  assert.equal(blits(second), 1, 'the key held: the bitmap fills and is blitted');
  assert.equal(arcs(made[0].rec.calls.filter(([k]) => k === 'arc')), 2);
  const third = frame();
  assert.deepEqual([arcs(third), blits(third)], [0, 1], 'nothing changed: one blit, no mark repainted');
  assert.equal(made.length, 1);
});

test('a point moved in place, a colour, a hover or a selection repaints the marks', () => {
  const { app, frame } = stage();
  frame(); frame();
  for (const change of [
    () => { app.lines[0].points[1].x = 9; },
    () => { app.lines[0].color = '#00ff00'; },
    () => { app.hoverPt = { lineIdx: 0, ptIdx: 0 }; },
    () => { app.selectedLineIdx = 0; },
    () => { app.lines[0].points.push({ x: 1, y: 1 }); },
    () => { app.showPoints = false; },
  ]) {
    frame(); frame();
    change();
    const calls = frame();
    assert.equal(blits(calls), 0, String(change));
    assert.ok(argsOf(calls, 'stroke').length > 0, String(change));
  }
});

test('a drag repaints straight onto the overlay every frame, never through the bitmap', () => {
  const { app, frame } = stage();
  frame(); frame();
  const painted = arcs(made[0].rec.calls);
  for (let i = 0; i < 5; i++) {
    app.lines[0].points[0].x = 3 + i;
    const calls = frame();
    assert.deepEqual([blits(calls), arcs(calls)], [0, 2]);
  }
  assert.equal(arcs(made[0].rec.calls), painted, 'the bitmap was not repainted mid-drag');
});

test('a stroke in flight is never cached, and the hold ghost always paints live', () => {
  const { app, frame } = stage();
  app.strokeFx.active = true;
  frame();
  assert.equal(blits(frame()), 0);
  assert.equal(made.length, 0);
  app.strokeFx.active = false;
  frame(); frame();
  for (const at of [5, 6]) {
    app.holdPreview = { x: at, y: 5 };
    const calls = frame();
    assert.deepEqual([blits(calls), arcs(calls)], [1, 1], 'the marks blit, then only the ghost point paints');
  }
});
