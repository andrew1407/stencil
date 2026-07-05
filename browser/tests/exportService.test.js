// Unit tests for ExportService (js/core/exportService.js) — the export/clipboard/file-IO
// cluster extracted out of DrawingApp. The service holds no state; it reads a back-referenced
// `app` and routes mutations through the app's shared methods. We drive it with a minimal fake
// app + a notify spy (notify() posts to a #notify-balloon element if present) and assert the
// guard branches and the shared #applyValidatedLayout routing used by upload + paste.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// notify() (utils.js) looks up #notify-balloon and calls its .notify(msg, type); spy on it.
const notifications = [];
const balloon = { notify: (msg, type) => notifications.push([msg, type]) };
globalThis.document = globalThis.document || {
  getElementById: (id) => (id === 'notify-balloon' ? balloon : null),
};

const { ExportService } = await import('../js/core/exportService.js');

// A fake app carrying just what the driven paths touch. `record` collects calls to the
// shared app methods so we can assert the service delegates instead of reimplementing.
const makeApp = (over = {}) => {
  const record = { saveHistory: 0, redraw: 0, updateButtons: 0, coordUpdate: [] };
  const app = {
    record,
    image: null,
    lines: [],
    canvas: { width: 100, height: 80 },
    confirm: async () => true,
    saveHistory() { record.saveHistory++; },
    renderer: { redraw() { record.redraw++; } },
    updateButtons() { record.updateButtons++; },
    coordTable: { update: (...a) => record.coordUpdate.push(a) },
    currentLayoutPayload: () => ({ lines: [] }),
    ...over,
  };
  return app;
};

const reset = () => { notifications.length = 0; };
const lastNote = () => notifications[notifications.length - 1];

test('saveImage: no image → fail notify, no work', () => {
  reset();
  new ExportService(makeApp()).saveImage();
  assert.deepEqual(lastNote(), ['No image loaded', 'fail']);
});

test('downloadJSON: no lines → fail notify', () => {
  reset();
  new ExportService(makeApp({ lines: [] })).downloadJSON();
  assert.deepEqual(lastNote(), ['No lines to export', 'fail']);
});

test('copyImageToClipboard: no image → fail notify', () => {
  reset();
  new ExportService(makeApp()).copyImageToClipboard();
  assert.deepEqual(lastNote(), ['No image to copy', 'fail']);
});

test('copyLayoutToClipboard: no lines → fail notify', () => {
  reset();
  new ExportService(makeApp({ lines: [] })).copyLayoutToClipboard();
  assert.deepEqual(lastNote(), ['No layout to copy', 'fail']);
});

test('applyPastedLayout: no image → "Load an image first", no mutation', async () => {
  reset();
  const app = makeApp({ image: null });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [] }] });
  assert.deepEqual(lastNote(), ['Load an image first', 'fail']);
  assert.equal(app.record.saveHistory, 0);
  assert.equal(app.record.redraw, 0);
});

test('applyPastedLayout: valid payload installs lines + routes through app methods', async () => {
  reset();
  const pasted = [{ points: [{ x: 1, y: 2 }], color: '#fff' }];
  const app = makeApp({ image: {}, lines: [] });
  // Match canvas dims so neither the replace nor the dim-mismatch confirm fires.
  await new ExportService(app).applyPastedLayout({ imageWidth: 100, imageHeight: 80, lines: pasted });
  // validateLayout accepted → lines installed and every shared method fired once.
  assert.equal(app.lines.length, 1);
  assert.equal(app.record.saveHistory, 1);
  assert.equal(app.record.redraw, 1);
  assert.equal(app.record.updateButtons, 1);
  assert.equal(app.record.coordUpdate.length, 1);
  assert.deepEqual(lastNote(), ['Layout pasted from clipboard', 'ok']);
});

test('applyPastedLayout: replace-confirm declined → canceled, no mutation', async () => {
  reset();
  // Existing lines + a valid payload triggers the replace confirm; decline it.
  const app = makeApp({ image: {}, lines: [{ points: [{ x: 0, y: 0 }] }], confirm: async () => false });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] });
  assert.deepEqual(lastNote(), ['Layout paste canceled', 'fail']);
  assert.equal(app.record.saveHistory, 0);
});
