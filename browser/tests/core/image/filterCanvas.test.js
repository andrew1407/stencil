// Every image filter is a strategy that paints ONE cached copy: a repaint blits it instead of
// re-running a CSS filter over the full-resolution image, and only a new image or tint rebuilds it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { recordingCtx } from '../../helpers/recordingCtx.js';

const built = [];
globalThis.document = {
  createElement: () => {
    const c = { width: 0, height: 0 };
    const rec = recordingCtx(c);
    c.calls = rec.calls;
    c.getContext = () => rec.ctx;
    built.push(c);
    return c;
  },
};
const { ImageFilterCanvas } = await import('../../../js/core/image/filterCanvas.js');
const { Renderer } = await import('../../../js/core/draw/renderer.js');

const IMG = { width: 4, height: 3 };

for (const [filter, css] of [['bw', 'grayscale(100%)'], ['sepia', 'sepia(100%)'], ['invert', 'invert(100%)']]) {
  test(`${filter}: painted once through ${css}, then served from the cache`, () => {
    built.length = 0;
    const cache = new ImageFilterCanvas();
    const a = cache.canvasFor(IMG, filter, null);
    const b = cache.canvasFor(IMG, filter, null);
    assert.equal(a, b);
    assert.equal(built.length, 1);
    assert.deepEqual([a.width, a.height], [4, 3]);
    assert.deepEqual(a.calls.map(([k, v]) => (k === 'drawImage' ? k : `${k}=${v}`)),
      [`set:filter=${css}`, 'drawImage', 'set:filter=none'], 'the filter wraps exactly one draw');
    assert.notEqual(cache.canvasFor({ width: 4, height: 3 }, filter, null), a, 'a new bitmap rebuilds');
  });
}

test('no filter paints no copy', () => {
  assert.equal(new ImageFilterCanvas().canvasFor(IMG, 'none', null), null);
  assert.equal(new ImageFilterCanvas().canvasFor(IMG, 'bogus', null), null);
});

test('the frame blits the cached copy with no CSS filter on the visible canvas', () => {
  const app = { image: IMG, imageFilter: 'sepia' };
  const r = new Renderer(app);
  const frames = [recordingCtx(), recordingCtx()];
  for (const { ctx } of frames) r.drawImageWithFilter(ctx);
  for (const { calls } of frames) {
    const draw = calls.find(([k]) => k === 'drawImage');
    assert.notEqual(draw[1], IMG, 'the copy, not the original, is drawn');
    assert.ok(!calls.some(([k, v]) => k === 'set:filter' && v !== 'none'), 'no filter on the frame');
  }
  assert.equal(frames[0].calls.find(([k]) => k === 'drawImage')[1], frames[1].calls.find(([k]) => k === 'drawImage')[1]);
  app.imageFilter = 'none';
  const plain = recordingCtx();
  r.drawImageWithFilter(plain.ctx);
  assert.deepEqual(plain.calls, [['drawImage', IMG, 0, 0]], 'unfiltered: the bitmap itself, filter untouched');
});
