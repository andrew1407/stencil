// The aqua painter (js/ui/aqua/image.js) over a recording context: every pixel painted once on
// the cell grid, and the blob is a PNG of the wallpaper's own size.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { AQUA } from '../../../js/ui/aqua/rules.js';
import { paintAquaImage, aquaImageBlob } from '../../../js/ui/aqua/image.js';

const recorder = () => {
  const rects = [];
  const ctx = { imageSmoothingEnabled: true, fillStyle: '', fillRect: (x, y, w, h) => rects.push([x, y, w, h, ctx.fillStyle]) };
  return { ctx, rects };
};

test('the painter fills the whole wallpaper in cell-aligned rects', () => {
  const { ctx, rects } = recorder();
  paintAquaImage(ctx);
  const { width, height, cell, ramp } = AQUA.image;
  assert.equal(ctx.imageSmoothingEnabled, false);
  assert.equal(rects.reduce((n, [, , w, h]) => n + w * h, 0), width * height, 'every pixel painted once');
  for (const [x, y, w, h] of rects) assert.ok([x, y, w, h].every((v) => v % cell === 0), 'on the grid');
  assert.deepEqual(rects[0], [0, 0, width, cell, ramp[0]]);
});

test('the blob is what the canvas encodes, at the wallpaper\'s size', async () => {
  let made = null;
  globalThis.document = {
    createElement: () => (made = {
      width: 0, height: 0,
      getContext: () => recorder().ctx,
      toBlob: (cb, type) => cb({ type, size: 1 }),
    }),
  };
  const blob = await aquaImageBlob();
  assert.equal(blob.type, 'image/png');
  assert.equal(made.width, AQUA.image.width);
  assert.equal(made.height, AQUA.image.height);
  delete globalThis.document;
});
