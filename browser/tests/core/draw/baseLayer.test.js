// BR-2 + BR-10: every frame composites ONE cached base per (image, filter, tint) — a hover repaint
// re-applies no filter — a duotone tint change of the picture on screen is painted by the image
// worker while the frame keeps the tint it already showed, and a filter switch or export builds now.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { recordingCtx } from '../../helpers/recordingCtx.js';
import { PixelCanvas, gradientImage } from '../../helpers/pixelCanvas.js';

const made = [];
globalThis.OffscreenCanvas = class extends PixelCanvas { constructor(w, h) { super(w, h); made.push(this); } };
globalThis.createImageBitmap = async (s) => s;

const pending = [];
let posts = 0;
class FakeWorker {
  constructor() { FakeWorker.live = this; }
  postMessage(msg, transfer) { posts++; pending.push({ msg, transfer }); }
  terminate() {}
}
const flush = () => new Promise((r) => setImmediate(r));
const bitmapOf = (msg) => ({ tag: `${msg.recipe}:${msg.rgb.r}`, width: msg.width, height: msg.height, close() { this.closed = true; } });
const deliver = async (reply = (msg) => ({ id: msg.id, ok: true, bitmap: bitmapOf(msg) })) => {
  const { msg } = pending.shift();
  const data = reply(msg);
  FakeWorker.live.onmessage({ data });
  await flush();
  return data.bitmap;
};

const { BaseLayer } = await import('../../../js/core/draw/baseLayer.js');
const { Renderer } = await import('../../../js/core/draw/renderer.js');

beforeEach(() => { made.length = 0; pending.length = 0; posts = 0; delete globalThis.Worker; });
const withWorker = () => { globalThis.Worker = FakeWorker; };

const appFor = (filter, image = gradientImage(6, 4)) => ({
  image, imageFilter: filter, filterColor: '#7c3aed', canvas: { width: 6, height: 4 },
  lines: [], showLines: true, selectedLines: [], selectedLineIdx: -1, compareMode: 'none',
});

for (const filter of ['none', 'bw', 'sepia', 'invert', 'contour', 'custom']) {
  test(`${filter}: a hover-only redraw composites the same cached base and re-applies no filter`, () => {
    const app = appFor(filter);
    const r = new Renderer(app);
    const sources = [];
    for (let i = 0; i < 5; i++) {
      const { ctx, calls } = recordingCtx(app.canvas);
      app.ctx = ctx;
      app.hoverPt = { lineIdx: 0, ptIdx: i };
      r.redraw();
      sources.push(calls.find(([k]) => k === 'drawImage')[1]);
      assert.ok(!calls.some(([k, v]) => k === 'set:filter' && v !== 'none'), 'no filter on the frame');
    }
    assert.equal(made.length, filter === 'none' ? 0 : 1, 'painted once, on the first frame');
    assert.ok(sources.every((s) => s === sources[0]));
    assert.equal(sources[0] === app.image, filter === 'none');
  });
}

test('only a new image, filter or tint rebuilds the base', () => {
  const layer = new BaseLayer();
  const a = gradientImage(3, 3);
  layer.frame(a, 'custom', '#7c3aed');
  layer.frame(a, 'custom', '#7c3aed');
  assert.equal(made.length, 1);
  layer.frame(a, 'custom', '#10b981');
  assert.equal(made.length, 2, 'a tint');
  layer.frame(a, 'contour', null);
  assert.equal(made.length, 3, 'a filter');
  layer.frame(gradientImage(3, 3), 'contour', null);
  assert.equal(made.length, 4, 'a crop, turn or load swaps the image');
});

test('a tint change of the pixel filter on screen is painted in the worker; the frame keeps the last tint meanwhile', async () => {
  withWorker();
  let ready = 0;
  const layer = new BaseLayer(() => ready++);
  const img = gradientImage(4, 3);
  const first = layer.frame(img, 'custom', '#010000');
  made.length = 0;
  assert.equal(layer.frame(img, 'custom', '#020000'), first, 'the last tint stays up');
  assert.equal(layer.frame(img, 'custom', '#020000'), first);
  assert.equal(posts, 1, 'one job per key');
  assert.deepEqual(pending[0].transfer, [pending[0].msg.data], 'the read pixels are transferred');
  assert.equal(pending[0].msg.task, 'filter');
  assert.equal(made.length, 1, 'only the read canvas: no copy painted on this thread');
  const bitmap = await deliver();
  assert.equal(ready, 1, 'the landing asks for a repaint');
  for (let i = 0; i < 3; i++) assert.equal(layer.frame(img, 'custom', '#020000'), bitmap);
  assert.equal(layer.now(img, 'custom', '#020000'), bitmap, 'export reads the landed copy');
  assert.equal(posts, 1);
});

const SWITCHES = [['none', null, 'contour', null], ['contour', null, 'custom', '#7c3aed'],
  ['custom', '#7c3aed', 'contour', null], ['bw', null, 'custom', '#10b981'], ['custom', '#7c3aed', 'none', null]];
for (const [from, fromTint, to, toTint] of SWITCHES) {
  test(`${from} → ${to}: the very next frame is the copy painted for the new filter`, () => {
    withWorker();
    const layer = new BaseLayer();
    const img = gradientImage(4, 3);
    const before = layer.frame(img, from, fromTint);
    const src = layer.frame(img, to, toTint);
    assert.notEqual(src, before, 'never the previous picture');
    const exact = new BaseLayer().now(img, to, toTint);
    if (to === 'none') assert.equal(src, img);
    else assert.deepEqual(src.px, exact.px, 'bit-identical to what now() builds');
    assert.equal(posts, 0, 'a switch never waits on the worker');
  });
}

test('a new picture is filtered now, never shown unfiltered', () => {
  withWorker();
  const layer = new BaseLayer();
  layer.frame(gradientImage(4, 3), 'contour', null);
  const next = gradientImage(4, 3);
  const src = layer.frame(next, 'contour', null);
  assert.notEqual(src, next);
  assert.ok(src instanceof PixelCanvas);
  assert.equal(posts, 0);
});

test('a tint drag shows each landed tint and paints only the newest next', async () => {
  withWorker();
  const layer = new BaseLayer();
  const img = gradientImage(4, 3);
  const first = layer.frame(img, 'custom', '#010000');
  assert.equal(layer.frame(img, 'custom', '#020000'), first);
  assert.equal(layer.frame(img, 'custom', '#030000'), first, 'one job at a time');
  assert.equal(posts, 1);
  const mid = await deliver();
  assert.equal(mid.tag, 'tint:2');
  assert.equal(layer.frame(img, 'custom', '#030000'), mid, 'the landed tint is on screen');
  assert.equal(pending[0].msg.rgb.r, 3, 'then the newest tint');
  const last = await deliver();
  assert.equal(layer.frame(img, 'custom', '#030000'), last);
  assert.equal(posts, 2);
});

test('a copy the worker could not paint is built on this thread, and never sent again', async () => {
  withWorker();
  let ready = 0;
  const layer = new BaseLayer(() => ready++);
  const img = gradientImage(4, 3);
  layer.frame(img, 'custom', '#010000');
  layer.frame(img, 'custom', '#020000');
  await deliver((msg) => ({ id: msg.id, ok: false, error: 'oom' }));
  assert.equal(ready, 1);
  const built = layer.frame(img, 'custom', '#020000');
  assert.ok(built instanceof PixelCanvas);
  assert.equal(layer.frame(img, 'custom', '#020000'), built);
  assert.equal(posts, 1);
});

test('export never waits on the worker: now() builds while a copy is being painted', () => {
  withWorker();
  const layer = new BaseLayer();
  const img = gradientImage(4, 3);
  const first = layer.frame(img, 'custom', '#010000');
  assert.equal(layer.frame(img, 'custom', '#020000'), first);
  const exported = layer.now(img, 'custom', '#020000');
  assert.ok(exported instanceof PixelCanvas && exported !== first);
  assert.equal(layer.frame(img, 'custom', '#020000'), exported, 'and the frame takes it too');
});

test('a landing for a picture or filter no longer on screen is dropped', async () => {
  withWorker();
  const layer = new BaseLayer();
  const img = gradientImage(4, 3);
  layer.frame(img, 'custom', '#010000');
  layer.frame(img, 'custom', '#020000');
  const next = gradientImage(4, 3);
  layer.frame(next, 'none', null);
  const stale = await deliver();
  assert.equal(stale.closed, true);
  assert.equal(layer.frame(next, 'none', null), next);
  layer.frame(img, 'custom', '#010000');
  layer.frame(img, 'custom', '#030000');
  const contour = layer.frame(img, 'contour', null);
  assert.equal((await deliver()).closed, true, 'the tint landed after a switch to contour');
  assert.equal(layer.frame(img, 'contour', null), contour);
});
