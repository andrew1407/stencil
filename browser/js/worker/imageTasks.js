// ── Image tasks: the imageWorker client and its inline fallback ──
// Downscale, thumbnail and contour renders go to worker/imageWorker.js with transferable
// bitmaps/buffers; without Workers or OffscreenCanvas — or in the single-file build, where
// the worker URL throws — the same imageRaster.js sequence runs inline on a document canvas.
import { core } from '../core/stencilCore.js';
import { applyContourRGBA } from '../core/contourFilter.js';
import { IMAGE_TASK } from './imageMessages.js';
import { fitSize, paintScaled, contourCanvas } from './imageRaster.js';

const pending = new Map();   // id → { resolve, reject }
let worker = null;
let dead = false;            // construction or the worker itself failed: inline from now on
let seq = 0;

export const imageWorkerUsable = () =>
  !dead && typeof Worker === 'function' && typeof OffscreenCanvas === 'function' && typeof createImageBitmap === 'function';

const failAll = (err) => {
  for (const p of pending.values()) p.reject(err);
  pending.clear();
};

const spawn = () => {
  if (worker) return worker;
  worker = new Worker(new URL('./imageWorker.js', import.meta.url), { type: 'module' });
  worker.onmessage = ({ data }) => {
    const p = pending.get(data.id);
    if (!p) return;
    pending.delete(data.id);
    if (data.ok) p.resolve(data.blob); else p.reject(new Error(data.error));
  };
  worker.onerror = (e) => {
    dead = true;
    worker.terminate();
    worker = null;
    failAll(new Error(e?.message || 'image worker failed'));
  };
  return worker;
};

const post = (msg, transfer) => new Promise((resolve, reject) => {
  const id = ++seq;
  pending.set(id, { resolve, reject });
  spawn().postMessage({ ...msg, id }, transfer);
});

const blobToDataUrl = (blob) => new Promise((resolve, reject) => {
  const r = new FileReader();
  r.onload = () => resolve(r.result);
  r.onerror = () => reject(r.error);
  r.readAsDataURL(blob);
});

const makeCanvas = (w, h) => {
  const c = document.createElement('canvas');
  c.width = w;
  c.height = h;
  return c;
};

// Worker first; a construction failure retires it, a task failure falls back for that call.
const run = async (job, inline) => {
  if (imageWorkerUsable()) {
    try {
      return await blobToDataUrl(await job());
    } catch (err) {
      if (!worker) dead = true;
    }
  }
  return inline();
};

// `source` is any CanvasImageSource the caller keeps alive (the worker gets a bitmap COPY,
// so the inline fallback can still draw it). `halve` is the thumbnail's step-down path.
export const downscaleInline = (source, sw, sh, { maxEdge, type = 'image/png', quality, halve = false }) =>
  paintScaled(makeCanvas, source, sw, sh, { ...fitSize(sw, sh, maxEdge), halve }).toDataURL(type, quality);

export const downscaleToDataUrl = (source, sw, sh, opts) => {
  const { maxEdge, type = 'image/png', quality, halve = false } = opts;
  const size = fitSize(sw, sh, maxEdge);
  return run(
    async () => {
      const bitmap = await createImageBitmap(source);
      return post({ task: IMAGE_TASK.SCALE, bitmap, ...size, halve, type, quality }, [bitmap]);
    },
    () => downscaleInline(source, sw, sh, opts));
};

// `readPixels` hands over a fresh ImageData per attempt: the worker copy is transferred
// (detached), so a fallback must read the canvas again. Inline prefers the wasm core.
export const contourToDataUrl = (readPixels, type = 'image/png') => run(
  () => {
    const { data, width, height } = readPixels();
    return post({ task: IMAGE_TASK.CONTOUR, data: data.buffer, width, height, type }, [data.buffer]);
  },
  () => contourCanvas(makeCanvas, readPixels(), core.op('applyContourRGBA') || applyContourRGBA).toDataURL(type));
