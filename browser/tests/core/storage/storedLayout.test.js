import { test } from 'node:test';
import assert from 'node:assert';
import { applyImagelessPayload } from '../../../js/core/storage/storedLayout.js';

// A stored layout with no picture (settings only, or lines waiting for a re-upload) empties the
// editor. The canvas element kept the OLD picture's backing size and inline CSS size, so the bare
// viewport still scrolled sideways and drew a bar under nothing (user report).

const stubApp = () => {
  const canvas = { width: 1600, height: 900, style: { width: '1600px', height: '900px' } };
  const calls = [];
  const app = {
    canvas, image: {}, originalImage: {}, cropRect: {}, rotationQuarters: 1, imageDataUrl: 'x', lines: [{}],
    history: { reset: () => calls.push('history') },
    updateInfo: () => calls.push('info'), updateButtons: () => calls.push('buttons'),
    renderer: { redraw: () => calls.push('redraw') },
    showSaveStatus: (...a) => calls.push(['status', a[0]]),
  };
  return { app, canvas, calls, storage: { app, showImageMissingBanner: (on) => calls.push(['banner', on]) } };
};

test('an image-less payload collapses the canvas element with the picture', () => {
  const { app, canvas, calls, storage } = stubApp();
  applyImagelessPayload(storage, { lines: [] });
  assert.strictEqual(app.image, null);
  assert.deepStrictEqual([canvas.width, canvas.height, canvas.style.width, canvas.style.height], [0, 0, '', '']);
  assert.ok(calls.includes('redraw') && calls.includes('buttons'), 'the empty state is repainted');
});

test('lines waiting for a re-upload still collapse the canvas', () => {
  const { app, canvas, calls, storage } = stubApp();
  applyImagelessPayload(storage, { lines: [{ points: [] }], imageWidth: 800, imageHeight: 600 });
  assert.deepStrictEqual(app.pendingImageSize, { w: 800, h: 600 });
  assert.strictEqual(canvas.width, 0);
  assert.ok(calls.some((c) => Array.isArray(c) && c[0] === 'banner' && c[1] === true));
});
