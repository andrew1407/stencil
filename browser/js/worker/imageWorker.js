// ── imageWorker: off-main-thread downscale / thumbnail / contour / filter builds / co-edit result ──
// A module Worker over the same imageRaster.js sequence the inline fallback runs and the same
// filterPixels.js pass the main thread runs, on the wasm core when it loads here, else its JS twin.
import { core } from '../core/abi/stencilCore.js';
import { applyContourRGBA } from '../core/image/contourFilter.js';
import { FILTER_RECIPE, filteredPixels } from '../core/image/filterPixels.js';
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

// The main thread's painter — its plain read when handed the picture, its RGBA pass, its putImageData
// of the result where it lies (a heap view, never copied back) — into a bitmap.
const filterBitmap = async ({ recipe, data, bitmap, width, height, rgb }) => {
  if (recipe !== FILTER_RECIPE.TINT) await coreReady();
  const canvas = makeCanvas(width, height);
  const ctx = canvas.getContext('2d');
  let image;
  if (bitmap) {
    try { ctx.drawImage(bitmap, 0, 0); } finally { bitmap.close(); }
    image = ctx.getImageData(0, 0, width, height);
  } else image = new ImageData(new Uint8ClampedArray(data), width, height);
  const out = filteredPixels(recipe, image.data, width, height, rgb, op);
  if (!out) throw new Error('wasm core unavailable in the image worker');
  ctx.putImageData(out === image.data ? image : new ImageData(out, width, height), 0, 0);
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
