// A point's 1 px ring contrasts with its own fill (js/core/line/render.js ringFor): black on a
// light fill, white on a dark one, at MARKER_RING.darkFromLuma — core markers::ringFor's twin.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ringFor, drawPoint } from '../../../js/core/line/render.js';
import { recordingCtx } from '../../helpers/recordingCtx.js';

test('ringFor: black on light fills, white on dark ones, in every form the canvas hands back', () => {
  assert.equal(ringFor('#ffff00'), '#000');
  assert.equal(ringFor('#fff'), '#000');
  assert.equal(ringFor('#ff0000'), '#fff');          // luma 54
  assert.equal(ringFor('#1e63c8'), '#fff');
  assert.equal(ringFor('rgba(255, 255, 0, 0.5)'), '#000');
  assert.equal(ringFor('rgba(0, 0, 128, 0.5)'), '#fff');
});

test('ringFor splits where the core does: luma 128 is light, 127 dark', () => {
  assert.equal(ringFor('#808080'), '#000');   // trunc(128.0) = 128
  assert.equal(ringFor('#7f7f7f'), '#fff');
});

test('drawPoint strokes the ring its fill asks for', () => {
  for (const [fill, ring] of [['#ffff00', '#000'], ['#ff0000', '#fff']]) {
    const { ctx } = recordingCtx({ width: 10, height: 10 });
    const strokes = [];
    const real = ctx.stroke.bind(ctx);
    ctx.stroke = () => { strokes.push(ctx.strokeStyle); real(); };
    drawPoint({ ctx, app: {} }, { x: 5, y: 5 }, fill, 3);
    assert.deepEqual(strokes, [ring]);
  }
});
