// BR-10: a filter copy the image worker paints is the copy the main thread paints, byte for byte —
// the same read, the same filterPixels pass, the same putImageData. JS twins here; the
// tests/wasm/ twin of this file repeats it with the wasm core loaded on both sides.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { PixelCanvas, PixelImageData, gradientImage } from '../helpers/pixelCanvas.js';

globalThis.OffscreenCanvas = PixelCanvas;
globalThis.ImageData = PixelImageData;
const replies = [];
globalThis.self = { postMessage: (msg, transfer) => replies.push({ msg, transfer }) };

const { core } = await import('../../js/core/abi/stencilCore.js');
core.init = async () => false;   // node --test never loads wasm: the worker falls back like the page
const { ImageFilterCanvas, readFilterInput } = await import('../../js/core/image/filterCanvas.js');
const { FILTER_RECIPE } = await import('../../js/core/image/filterPixels.js');
await import('../../js/worker/imageWorker.js');

const ask = async (msg) => {
  replies.length = 0;
  await self.onmessage({ data: { id: 7, task: 'filter', ...msg } });
  assert.equal(replies.length, 1);
  return replies[0];
};

const viaWorker = async (image, filter, color) => {
  const { recipe, pixels, rgb } = readFilterInput(image, filter, color);
  const { msg, transfer } = await ask({ recipe, data: pixels.data.buffer, width: pixels.width, height: pixels.height, rgb });
  assert.equal(msg.ok, true, msg.error);
  assert.deepEqual(transfer, [msg.bitmap], 'the bitmap is transferred, not copied');
  return msg.bitmap;
};

for (const [filter, color] of [['contour', null], ['custom', '#7c3aed'], ['custom', '#10b981']]) {
  test(`${filter}${color ? ` ${color}` : ''}: the worker's copy is the main thread's, byte for byte`, async () => {
    const image = gradientImage(23, 17);
    const main = new ImageFilterCanvas().canvasFor(image, filter, color);
    const worker = await viaWorker(image, filter, color);
    assert.deepEqual([worker.width, worker.height], [23, 17]);
    assert.deepEqual(worker.px, main.px);
    assert.notDeepEqual(main.px, image.px, 'the filter changed the pixels');
  });
}

test('without wasm the duotone reads grayscaled pixels and the worker only tints them', () => {
  assert.equal(readFilterInput(gradientImage(2, 2), 'custom', '#7c3aed').recipe, FILTER_RECIPE.TINT);
  assert.equal(readFilterInput(gradientImage(2, 2), 'contour', null).recipe, FILTER_RECIPE.CONTOUR);
  assert.equal(readFilterInput(gradientImage(2, 2), 'sepia', null), null, 'a CSS filter has no RGBA pass');
});

test('a worker without the wasm core refuses the one-pass duotone instead of painting other pixels', async () => {
  const { msg } = await ask({ recipe: FILTER_RECIPE.CUSTOM, data: new ArrayBuffer(4), width: 1, height: 1, rgb: { r: 1, g: 2, b: 3 } });
  assert.equal(msg.ok, false);
  assert.match(msg.error, /wasm core unavailable/);
});
