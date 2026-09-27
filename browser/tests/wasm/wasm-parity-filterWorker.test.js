// BR-10 on the wasm core: the image worker loads the core the page loaded, so its filter copy is
// the main thread's byte for byte — the one-pass duotone included, which only wasm paints.
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { PixelCanvas, PixelImageData, gradientImage } from '../helpers/pixelCanvas.js';

const MODULE_BUILT = existsSync(fileURLToPath(new URL('../../js/wasm/stencilCore.js', import.meta.url)));
const wtest = MODULE_BUILT ? test : test.skip;

globalThis.OffscreenCanvas = PixelCanvas;
globalThis.ImageData = PixelImageData;
const replies = [];
globalThis.self = { postMessage: (msg) => replies.push(msg) };

const { core } = await import('../../js/core/abi/stencilCore.js');
const { ImageFilterCanvas, readFilterInput } = await import('../../js/core/image/filterCanvas.js');
const { FILTER_RECIPE } = await import('../../js/core/image/filterPixels.js');
const { applyContourRGBA } = await import('../../js/core/image/contourFilter.js');
await import('../../js/worker/imageWorker.js');

before(async () => {
  if (!MODULE_BUILT) return;
  assert.equal(await core.init(), true, 'wasm core must load in Node (SINGLE_FILE ES module)');
});

const viaWorker = async (image, filter, color) => {
  const { recipe, pixels, rgb } = readFilterInput(image, filter, color);
  replies.length = 0;
  await self.onmessage({ data: { id: 1, task: 'filter', recipe, data: pixels.data.buffer, width: pixels.width, height: pixels.height, rgb } });
  assert.equal(replies[0].ok, true, replies[0].error);
  return replies[0].bitmap.px;
};

for (const [filter, color] of [['contour', null], ['custom', '#7c3aed'], ['custom', '#f59e0b']]) {
  wtest(`${filter}${color ? ` ${color}` : ''}: worker and main thread agree on wasm`, async () => {
    const image = gradientImage(31, 19);
    assert.deepEqual(await viaWorker(image, filter, color), new ImageFilterCanvas().canvasFor(image, filter, color).px);
  });
}

wtest('with wasm the duotone is the one-pass recipe, and the contour is the JS twin byte for byte', async () => {
  const image = gradientImage(9, 5);
  assert.equal(readFilterInput(image, 'custom', '#7c3aed').recipe, FILTER_RECIPE.CUSTOM);
  const js = Uint8ClampedArray.from(image.px);
  applyContourRGBA(js, 9, 5);
  assert.deepEqual(await viaWorker(image, 'contour', null), js);
});
