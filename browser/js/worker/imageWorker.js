// ── imageWorker: off-main-thread downscale / thumbnail / contour ──
// A module Worker over the same imageRaster.js sequence the inline fallback runs. The
// contour is the JS reference (wasm never loads here) — byte-identical by the parity contract.
import { applyContourRGBA } from '../core/image/contourFilter.js';
import { IMAGE_TASK } from './imageMessages.js';
import { paintScaled, contourCanvas } from './imageRaster.js';

const makeCanvas = (w, h) => new OffscreenCanvas(w, h);

const render = (msg) => {
  if (msg.task === IMAGE_TASK.SCALE) {
    try {
      return paintScaled(makeCanvas, msg.bitmap, msg.bitmap.width, msg.bitmap.height, msg);
    } finally {
      msg.bitmap.close();
    }
  }
  if (msg.task === IMAGE_TASK.CONTOUR) {
    const image = new ImageData(new Uint8ClampedArray(msg.data), msg.width, msg.height);
    return contourCanvas(makeCanvas, image, applyContourRGBA);
  }
  throw new Error(`unknown image task "${msg.task}"`);
};

self.onmessage = async ({ data: msg }) => {
  try {
    const blob = await render(msg).convertToBlob({ type: msg.type, quality: msg.quality });
    self.postMessage({ id: msg.id, ok: true, blob });
  } catch (err) {
    self.postMessage({ id: msg.id, ok: false, error: String(err?.message || err) });
  }
};
