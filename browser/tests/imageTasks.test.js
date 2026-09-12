import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { IMAGE_TASK } from '../js/worker/imageMessages.js';
import { fitSize, paintScaled, contourCanvas } from '../js/worker/imageRaster.js';

// A recording canvas: every drawImage lands in `ops` with the destination size and the
// smoothing flags in force, so the halving sequence can be pinned without pixels.
const ops = [];
const fakeCanvas = (width, height) => {
  const c = { width, height, kind: 'canvas' };
  const ctx = {
    imageSmoothingEnabled: false, imageSmoothingQuality: 'low',
    drawImage(src, x, y, w, h) { ops.push({ from: [src.width, src.height], to: [w, h], hq: ctx.imageSmoothingQuality }); },
    putImageData(d) { c.put = d; },
  };
  c.getContext = () => ctx;
  c.toDataURL = (type, q) => `data:${type};q=${q ?? ''},${c.width}x${c.height}`;
  return c;
};
beforeEach(() => { ops.length = 0; });

test('IMAGE_TASK is a frozen table of the two worker tasks', () => {
  assert.deepEqual(IMAGE_TASK, { SCALE: 'scale', CONTOUR: 'contour' });
  assert.ok(Object.isFrozen(IMAGE_TASK));
});

test('fitSize caps the long edge, never upscales, keeps a pixel', () => {
  assert.deepEqual(fitSize(2657, 1771, 480), { width: 480, height: 320 });
  assert.deepEqual(fitSize(300, 200, 480), { width: 300, height: 200 });
  assert.deepEqual(fitSize(4000, 1, 480), { width: 480, height: 1 });
});

test('paintScaled without halving is one plain draw (the chat attachment path)', () => {
  const out = paintScaled(fakeCanvas, { width: 3000, height: 2000 }, 3000, 2000, { width: 1568, height: 1045 });
  assert.deepEqual(ops, [{ from: [3000, 2000], to: [1568, 1045], hq: 'low' }]);
  assert.equal(out.width, 1568);
});

test('paintScaled with halving steps down 2x until within 2x of the target, high quality', () => {
  // 2657 → 1329 → 665 → (665 > 960? no) final 480: the thumbnail\'s aliasing guard.
  paintScaled(fakeCanvas, { width: 2657, height: 1771 }, 2657, 1771, { width: 480, height: 320, halve: true });
  assert.deepEqual(ops.map((o) => o.to), [[1329, 886], [665, 443], [480, 320]]);
  assert.ok(ops.every((o) => o.hq === 'high'));
});

test('contourCanvas runs the pass in place and paints the same ImageData back', () => {
  const data = new Uint8ClampedArray([1, 2, 3, 4]);
  const image = { data, width: 1, height: 1 };
  const seen = [];
  const out = contourCanvas(fakeCanvas, image, (d, w, h) => { seen.push([d, w, h]); d[0] = 9; });
  assert.deepEqual(seen, [[data, 1, 1]]);
  assert.equal(out.put, image);
  assert.equal(data[0], 9);
});

// ── The client: worker path with fake globals, and the inline fallback ──
const G = globalThis;
const saved = {};
const install = (globals) => {
  for (const [k, v] of Object.entries(globals)) { saved[k] = G[k]; G[k] = v; }
};
afterEach(() => { for (const [k, v] of Object.entries(saved)) { if (v === undefined) delete G[k]; else G[k] = v; } for (const k in saved) delete saved[k]; });

// One fresh module per test: the client memoizes its worker and its dead flag.
const freshClient = () => import(`../js/worker/imageTasks.js?t=${Math.random()}`);

const fakeDom = () => ({ createElement: (tag) => { assert.equal(tag, 'canvas'); return fakeCanvas(0, 0); } });

// A Worker double that answers on the next tick with `reply(msg)`, recording posts.
const makeWorker = (reply) => {
  const posted = [];
  class Worker {
    constructor(url, opts) { Worker.made.push({ url: String(url), opts }); }
    postMessage(msg, transfer) { posted.push({ msg, transfer }); queueMicrotask(() => this.onmessage({ data: reply(msg) })); }
    terminate() { Worker.terminated++; }
  }
  Worker.made = []; Worker.terminated = 0;
  return { Worker, posted };
};
class FileReader {
  readAsDataURL(blob) { queueMicrotask(() => { this.result = `data:${blob.type};base64,${blob.bytes}`; this.onload(); }); }
}

test('inline fallback: no Worker in scope → a document canvas, same sequence, toDataURL', async () => {
  install({ document: fakeDom() });
  const { downscaleToDataUrl, contourToDataUrl, imageWorkerUsable } = await freshClient();
  assert.equal(imageWorkerUsable(), false);
  const url = await downscaleToDataUrl({ width: 1000, height: 500 }, 1000, 500, { maxEdge: 480, type: 'image/jpeg', quality: 0.85, halve: true });
  assert.equal(url, 'data:image/jpeg;q=0.85,480x240');
  assert.deepEqual(ops.map((o) => o.to), [[500, 250], [480, 240]]);
  const reads = [];
  const edge = await contourToDataUrl(() => { reads.push(1); return { data: new Uint8ClampedArray(4), width: 1, height: 1 }; });
  assert.equal(edge, 'data:image/png;q=,1x1');
  assert.equal(reads.length, 1);
});

test('worker path: a scale task transfers a bitmap copy and the reply blob becomes the data URL', async () => {
  const { Worker, posted } = makeWorker((msg) => ({ id: msg.id, ok: true, blob: { type: msg.type, bytes: `${msg.width}x${msg.height}:${msg.halve}` } }));
  const bitmaps = [];
  install({
    Worker, FileReader, OffscreenCanvas: class {},
    createImageBitmap: async (src) => { const b = { width: src.width, height: src.height, copyOf: src }; bitmaps.push(b); return b; },
    document: fakeDom(),
  });
  const { downscaleToDataUrl, imageWorkerUsable } = await freshClient();
  assert.equal(imageWorkerUsable(), true);
  const src = { width: 2000, height: 1000 };
  const url = await downscaleToDataUrl(src, 2000, 1000, { maxEdge: 480, type: 'image/jpeg', quality: 0.85, halve: true });
  assert.equal(url, 'data:image/jpeg;base64,480x240:true');
  assert.equal(Worker.made.length, 1);
  assert.match(Worker.made[0].url, /worker\/imageWorker\.js$/);
  assert.deepEqual(Worker.made[0].opts, { type: 'module' });
  assert.equal(posted.length, 1);
  assert.equal(posted[0].msg.task, IMAGE_TASK.SCALE);
  assert.equal(posted[0].msg.bitmap, bitmaps[0]);
  assert.deepEqual(posted[0].transfer, [bitmaps[0]]);
  assert.equal(ops.length, 0, 'nothing drawn on the main thread');
});

test('worker path: a contour task transfers the pixel buffer; a task error falls back inline for that call', async () => {
  let fail = true;
  const { Worker, posted } = makeWorker((msg) => (fail ? { id: msg.id, ok: false, error: 'oom' } : { id: msg.id, ok: true, blob: { type: msg.type, bytes: 'edge' } }));
  install({ Worker, FileReader, OffscreenCanvas: class {}, createImageBitmap: async (s) => s, document: fakeDom() });
  const { contourToDataUrl, imageWorkerUsable } = await freshClient();
  const reads = [];
  const readPixels = () => { const d = new Uint8ClampedArray(8); reads.push(d.buffer); return { data: d, width: 2, height: 1 }; };
  assert.equal(await contourToDataUrl(readPixels), 'data:image/png;q=,2x1', 'the failed task fell back inline');
  assert.equal(reads.length, 2, 'a fresh ImageData per attempt — the transferred one is detached');
  assert.equal(posted[0].msg.task, IMAGE_TASK.CONTOUR);
  assert.deepEqual(posted[0].transfer, [reads[0]]);
  assert.equal(imageWorkerUsable(), true, 'one bad task does not retire the worker');
  fail = false;
  assert.equal(await contourToDataUrl(readPixels), 'data:image/png;base64,edge');
});

test('a worker that cannot be constructed (the single-file build) retires itself: inline from then on', async () => {
  class Worker { constructor() { throw new Error('single-file build: no module worker'); } }
  install({ Worker, FileReader, OffscreenCanvas: class {}, createImageBitmap: async (s) => s, document: fakeDom() });
  const { downscaleToDataUrl, imageWorkerUsable } = await freshClient();
  assert.equal(imageWorkerUsable(), true);
  assert.equal(await downscaleToDataUrl({ width: 10, height: 10 }, 10, 10, { maxEdge: 5 }), 'data:image/png;q=,5x5');
  assert.equal(imageWorkerUsable(), false);
  assert.equal(await downscaleToDataUrl({ width: 10, height: 10 }, 10, 10, { maxEdge: 5 }), 'data:image/png;q=,5x5');
});

test('a worker error event rejects every pending task and retires the worker', async () => {
  const posted = [];
  let instance;
  class Worker {
    constructor() { instance = this; }
    postMessage(msg) { posted.push(msg); if (posted.length === 2) queueMicrotask(() => this.onerror({ message: 'boom' })); }
    terminate() { this.dead = true; }
  }
  install({ Worker, FileReader, OffscreenCanvas: class {}, createImageBitmap: async (s) => s, document: fakeDom() });
  const { downscaleToDataUrl, imageWorkerUsable } = await freshClient();
  const a = downscaleToDataUrl({ width: 10, height: 10 }, 10, 10, { maxEdge: 5 });
  const b = downscaleToDataUrl({ width: 20, height: 20 }, 20, 20, { maxEdge: 5 });
  assert.deepEqual(await Promise.all([a, b]), ['data:image/png;q=,5x5', 'data:image/png;q=,5x5']);
  assert.equal(instance.dead, true);
  assert.equal(imageWorkerUsable(), false);
});
