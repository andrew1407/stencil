// Plan 7b: the co-edit result renders in the image worker when it can and on this thread
// otherwise, and both produce exactly what the export path painted before — the base, then the
// resting lines. Every canvas here "encodes" its own recorded paint, so equal bytes mean an equal
// op stream, the worker's module run for real behind a stand-in Worker.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { recordingCtx } from '../helpers/recordingCtx.js';

// The base arrives as the image itself inline and as a bitmap copy in the worker: one name for both.
const encode = (calls) => new Blob([JSON.stringify(calls.map(([k, ...a]) => (k === 'drawImage' ? [k, 'BASE', ...a.slice(1)] : [k, ...a])))]);
class RecCanvas {
  constructor(width = 0, height = 0) { this.width = width; this.height = height; this.rec = recordingCtx(this); }
  getContext() { return this.rec.ctx; }
  convertToBlob() { return Promise.resolve(encode(this.rec.calls)); }
  toBlob(cb) { cb(encode(this.rec.calls)); }
}
globalThis.OffscreenCanvas = RecCanvas;
globalThis.document = { createElement: () => new RecCanvas() };
globalThis.createImageBitmap = async (src) => ({ width: src.width, height: src.height, close() { this.closed = true; } });

// A Worker whose other side is the real worker/imageWorker.js, over a message channel of calls.
let posted = [];
class BridgedWorker {
  constructor() { BridgedWorker.live = this; }
  postMessage(msg, transfer) {
    posted.push({ msg, transfer });
    queueMicrotask(() => globalThis.self.onmessage({ data: msg }));
  }
  terminate() {}
}
globalThis.self = { postMessage: (reply) => BridgedWorker.live.onmessage({ data: reply }) };
await import('../../js/worker/imageWorker.js');

const { resultPngBytes } = await import('../../js/worker/imageTasks.js');
const { restingJob } = await import('../../js/core/draw/restingPaint.js');
const { Renderer } = await import('../../js/core/draw/renderer.js');
const { ExportService } = await import('../../js/core/export/service.js');

const LINES = [
  { points: [{ x: 1, y: 1 }, { x: 9, y: 2 }, { x: 4, y: 7 }], color: '#c81e1e', thickness: 3, style: 'solid',
    locked: true, fillColor: '#00ff0080', pointSize: 5 },
  { points: [{ x: 2, y: 8 }, { x: 8, y: 8 }], color: '#1e63c8', thickness: 2, style: 'dashed', pointColor: '#ffaa00' },
  { points: [{ x: 5, y: 5 }], color: '#222222', thickness: 1, style: 'dotted' },
];
const makeApp = (over = {}) => {
  const app = {
    image: Object.assign(new RecCanvas(12, 10), { tag: 'IMG' }), imageFilter: 'none', canvas: { width: 12, height: 10 },
    lines: structuredClone(LINES), showLines: true, showPoints: true, pointSize: 4, compareMode: 'none',
    strokeFx: { suspend() {}, resume() {}, pointsOf: (l) => l.points, scaleAt: () => 1, paintUnder() {}, paintOver() {} },
    ...over,
  };
  app.renderer = new Renderer(app);
  return app;
};
const text = async (bytes) => Buffer.from(bytes).toString();
const exported = async (app) => {
  const off = new ExportService(app).renderExportCanvas('current');
  return text(await off.convertToBlob().then((b) => b.arrayBuffer()));
};

beforeEach(() => { posted = []; delete globalThis.Worker; });

for (const [name, over] of [['lines and points', {}], ['lines alone', { showPoints: false }], ['points alone', { showLines: false }]]) {
  test(`${name}: the worker, the inline fallback and the export path paint the same result`, async () => {
    const app = makeApp(over);
    const inline = await text(await resultPngBytes(restingJob(app)));
    globalThis.Worker = BridgedWorker;
    const worker = await text(await resultPngBytes(restingJob(app)));
    assert.equal(posted.length, 1, 'the worker painted it');
    assert.equal(posted[0].msg.task, 'result');
    assert.deepEqual(posted[0].transfer, [posted[0].msg.bitmap], 'the base crosses as a transferred bitmap');
    assert.equal(posted[0].msg.bitmap.closed, true, 'and the worker closes it');
    assert.equal(worker, inline, 'byte for byte');
    assert.equal(inline, await exported(app), 'what the export path painted');
    assert.ok(JSON.parse(inline).length > 2, 'something was painted');
  });
}

test('the job is a snapshot: edits after the capture never reach the result', async () => {
  const app = makeApp();
  const job = restingJob(app);
  const before = await text(await resultPngBytes(restingJob(app)));
  app.lines[0].points[0].x = 99;
  app.lines.push({ points: [{ x: 0, y: 0 }, { x: 3, y: 3 }], color: '#000000', thickness: 1 });
  app.showPoints = false;
  assert.equal(await text(await resultPngBytes(job)), before);
});

test('a worker that fails paints the result on this thread instead', async () => {
  const app = makeApp();
  const inline = await text(await resultPngBytes(restingJob(app)));
  class FailingWorker extends BridgedWorker {
    postMessage(msg) { queueMicrotask(() => this.onmessage({ data: { id: msg.id, ok: false, error: 'oom' } })); }
  }
  globalThis.Worker = FailingWorker;
  assert.equal(await text(await resultPngBytes(restingJob(app))), inline);
});
