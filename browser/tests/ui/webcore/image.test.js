// The webcore painter (js/ui/webcore/image.js) over a recording context: every rect lands on
// the cell grid, the clouds come last, and the blob is a PNG of the picture's own size.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { WEBCORE, cloudBlocks } from '../../../js/ui/webcore/rules.js';
import { paintWebcoreImage, webcoreImageBlob } from '../../../js/ui/webcore/image.js';

const recorder = () => {
  const rects = [];
  const ctx = { imageSmoothingEnabled: true, fillStyle: '', fillRect: (x, y, w, h) => rects.push([x, y, w, h, ctx.fillStyle]) };
  return { ctx, rects };
};

test('the painter fills the whole picture in cell-aligned rects, the clouds on top', () => {
  const { ctx, rects } = recorder();
  paintWebcoreImage(ctx);
  const { width, height, cell } = WEBCORE.image;
  assert.equal(ctx.imageSmoothingEnabled, false);
  const clouds = cloudBlocks().length;
  const base = rects.slice(0, rects.length - clouds);
  assert.equal(base.reduce((n, [, , w, h]) => n + w * h, 0), width * height, 'every pixel painted once');
  for (const [x, y, w, h] of rects) assert.ok([x, y, w, h].every((v) => v % cell === 0), 'on the grid');
  assert.deepEqual(rects[0], [0, 0, width, cell, WEBCORE.image.skyBands[0]]);
  assert.equal(rects.at(-1)[4], WEBCORE.image.cloudShade);
});

test('the blob is what the canvas encodes, at the picture\'s size', async () => {
  let made = null;
  globalThis.document = {
    createElement: () => (made = {
      width: 0, height: 0,
      getContext: () => recorder().ctx,
      toBlob: (cb, type) => cb({ type, size: 1 }),
    }),
  };
  const blob = await webcoreImageBlob();
  assert.equal(blob.type, 'image/png');
  assert.equal(made.width, WEBCORE.image.width);
  assert.equal(made.height, WEBCORE.image.height);
  delete globalThis.document;
});
