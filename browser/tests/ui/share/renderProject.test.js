// ui/share/renderProject.js: a project that is not open is drawn the way the editor draws it — the
// crop sizes the canvas, the turn and mirror are one transform, the lines come only with "current"
// — and the decoded bitmap is closed whatever the variant.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { recordingCtx, argsOf } from '../../helpers/recordingCtx.js';
import { orientedCrop, renderProject } from '../../../js/ui/share/renderProject.js';

const canvases = [];
globalThis.document ??= {};
globalThis.document.createElement = (tag) => {
  assert.equal(tag, 'canvas');
  const canvas = { width: 0, height: 0, toBlob: (cb, type) => cb(new Blob(['png'], { type })) };
  const rec = recordingCtx(canvas);
  canvas.getContext = () => rec.ctx;
  canvas.calls = rec.calls;
  canvases.push(canvas);
  return canvas;
};
let closed = 0;
globalThis.createImageBitmap = async () => ({ width: 40, height: 20, close: () => { closed += 1; } });

const layout = { cropRect: { x: 2, y: 4, w: 10, h: 30 }, rotationQuarters: 1, mirrored: true, imageFilter: 'none',
  lines: [{ id: 'l1', points: [{ x: 1, y: 1 }, { x: 5, y: 9 }], color: '#ff0000', thickness: 2 }] };

test('the crop sizes the canvas; the quarter-turn, the offset and the mirror are one transform', () => {
  canvases.length = 0;
  const c = orientedCrop({ width: 40, height: 20 }, layout);
  assert.deepEqual([c.width, c.height], [10, 30]);
  assert.deepEqual(argsOf(c.calls, 'translate'), [[20 / 2 - 2, 40 / 2 - 4]]);
  assert.deepEqual(argsOf(c.calls, 'rotate'), [[Math.PI / 2]]);
  assert.deepEqual(argsOf(c.calls, 'scale'), [[-1, 1]]);
  const whole = orientedCrop({ width: 40, height: 20 }, {});
  assert.deepEqual([whole.width, whole.height], [40, 20], 'no crop is the whole picture');
});

test('a filtered picture encodes from the filter\'s OffscreenCanvas, which has no toBlob', async () => {
  const encoded = [];
  globalThis.OffscreenCanvas = class {
    constructor(width, height) { this.width = width; this.height = height; this.rec = recordingCtx(this); }
    getContext() { return this.rec.ctx; }
    convertToBlob({ type }) { encoded.push([this.width, this.height]); return Promise.resolve(new Blob(['png'], { type })); }
  };
  try {
    const bw = { ...layout, imageFilter: 'bw' };
    for (const variant of ['tint', 'current', 'original']) {
      const png = await renderProject(new Blob(['x']), bw, variant, {});
      assert.equal(png.type, 'image/png', variant);
    }
    assert.deepEqual(encoded, [[10, 30]], 'only the no-lines picture is the filter\'s own canvas');
  } finally {
    delete globalThis.OffscreenCanvas;
  }
});

test('original and no-lines paint no line; current paints them; the bitmap closes every time', async () => {
  closed = 0;
  for (const variant of ['original', 'tint', 'current']) {
    canvases.length = 0;
    const png = await renderProject(new Blob(['x']), layout, variant, { showLines: true, showPoints: true, pointSize: 4 });
    assert.equal(png.type, 'image/png');
    const strokes = canvases.flatMap((c) => argsOf(c.calls, 'stroke'));
    assert.equal(strokes.length > 0, variant === 'current', `${variant}: lines only with the current picture`);
  }
  assert.equal(closed, 3);
  await assert.rejects(renderProject(new Blob(['x']), { cropRect: 'bad', lines: 'bad' }, 'current', {})
    .then(() => { throw new Error('ok'); }), /ok/, 'a malformed layout still renders the whole picture');
  assert.equal(closed, 4);
});
