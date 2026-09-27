// ── imageWorker: off-main-thread downscale / thumbnail / contour / filter builds / co-edit result ──
// A module Worker over the same imageRaster.js sequence the inline fallback runs and the same
// filterPixels.js pass the main thread runs, on the wasm core when it loads here, else its JS twin.
import { core } from '../core/abi/stencilCore.js';
import { applyContourRGBA } from '../core/image/contourFilter.js';
import { FILTER_RECIPE, filterPixels } from '../core/image/filterPixels.js';
import { IMAGE_TASK } from './imageMessages.js';
import { paintScaled, contourCanvas, paintResult } from './imageRaster.js';

const makeCanvas = (w, h) => new OffscreenCanvas(w, h);
const op = (name) => core.op(name);
let loading = null;
// Settles once: the wasm core installed, or the JS twins in its place.
const coreReady = () => (loading ||= core.init());

const render = async (msg) => {
  if (msg.task === IMAGE_TASK.SCALE) {
    try {
      return paintScaled(makeCanvas, msg.bitmap, msg.bitmap.width, msg.bitmap.height, msg);
    } finally {
      msg.bitmap.close();
    }
  }
  if (msg.task === IMAGE_TASK.RESULT) {
    try {
      return paintResult(makeCanvas, msg.bitmap, msg);
    } finally {
      msg.bitmap.close();
    }
  }
  if (msg.task === IMAGE_TASK.CONTOUR) {
    await coreReady();
    const image = new ImageData(new Uint8ClampedArray(msg.data), msg.width, msg.height);
    return contourCanvas(makeCanvas, image, op('applyContourRGBA') || applyContourRGBA);
  }
  throw new Error(`unknown image task "${msg.task}"`);
};

// The RGBA pass the main thread's painter runs, then its putImageData, into a bitmap.
const filterBitmap = async ({ recipe, data, width, height, rgb }) => {
  if (recipe !== FILTER_RECIPE.TINT) await coreReady();
  const image = new ImageData(new Uint8ClampedArray(data), width, height);
  if (!filterPixels(recipe, image.data, width, height, rgb, op)) throw new Error('wasm core unavailable in the image worker');
  const canvas = makeCanvas(width, height);
  canvas.getContext('2d').putImageData(image, 0, 0);
  return canvas.transferToImageBitmap();
};

self.onmessage = async ({ data: msg }) => {
  try {
    if (msg.task === IMAGE_TASK.FILTER) {
      const bitmap = await filterBitmap(msg);
      self.postMessage({ id: msg.id, ok: true, bitmap }, [bitmap]);
      return;
    }
    const blob = await (await render(msg)).convertToBlob({ type: msg.type, quality: msg.quality });
    self.postMessage({ id: msg.id, ok: true, blob });
  } catch (err) {
    self.postMessage({ id: msg.id, ok: false, error: String(err?.message || err) });
  }
};
