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

// ── .stencil project file save/open ─────────────────────────────────────────
// A valid serialized project the real parseProjectFile accepts (format + v1 + embedded image).
const VALID_STENCIL = (name) => JSON.stringify({
  format: 'stencil-project', version: 1, name,
  image: { dataUrl: 'data:image/png;base64,AAAA', ext: 'png', w: 2, h: 2 }, layout: {},
});

test('saveProjectFile: no image → "Open an image first", no work', async () => {
  reset();
  await new ExportService(makeApp()).saveProjectFile();   // no image/imageDataUrl
  assert.deepEqual(lastNote(), ['Open an image first', 'fail']);
});

test('saveProjectFile: FS Access path writes the serialized project + links for live-sync', async () => {
  reset();
  const writes = [];
  let linked = null;
  const handle = {
    name: 'plan.stencil',
    createWritable: async () => ({ write: async (t) => writes.push(t), close: async () => {} }),
  };
  globalThis.window = { showSaveFilePicker: async () => handle };
  const app = makeApp({
    image: {}, imageDataUrl: 'data:image/png;base64,AAAA', activeProjectId: 'p1',
    imageBaseName: 'plan',
    projectFileState: () => ({ name: 'plan', image: { dataUrl: 'data:image/png;base64,AAAA', ext: 'png', w: 2, h: 2 }, layout: {} }),
    storage: { store: { getMeta: () => ({ name: 'plan' }) } },
    stencilSync: { link: async (h, n) => { linked = [h, n]; } },
  });
  try {
    await new ExportService(app).saveProjectFile();
    assert.equal(writes.length, 1);
    const doc = JSON.parse(writes[0]);
    assert.equal(doc.format, 'stencil-project');
    assert.equal(doc.name, 'plan');
    assert.deepEqual(linked, [handle, 'plan.stencil']);   // handle kept for auto-save/watch
    assert.deepEqual(lastNote(), ['Project saved', 'ok']);
  } finally { delete globalThis.window; }
});

test('saveProjectFile: user-cancelled Save picker (AbortError) is silent', async () => {
  reset();
  globalThis.window = { showSaveFilePicker: async () => { const e = new Error('x'); e.name = 'AbortError'; throw e; } };
  const app = makeApp({
    image: {}, imageDataUrl: 'data:image/png;base64,AAAA', activeProjectId: 'p1', imageBaseName: 'p',
    projectFileState: () => ({ name: 'p', layout: {} }),
    storage: { store: { getMeta: () => ({ name: 'p' }) } },
    stencilSync: { link: async () => {} },
  });
  try {
    await new ExportService(app).saveProjectFile();
    assert.equal(notifications.length, 0);   // cancel is not an error
  } finally { delete globalThis.window; }
});

test('openProjectFile: invalid text → "Invalid .stencil file" notify, no apply', async () => {
  reset();
  let applied = 0;
  const app = makeApp({ applyProjectFile: async () => { applied++; return 'x'; } });
  await new ExportService(app).openProjectFile('not a project at all');
  assert.equal(applied, 0);
  assert.match(lastNote()[0], /^Invalid \.stencil file: /);
  assert.equal(lastNote()[1], 'fail');
});

test('openProjectFile: valid text routes to applyProjectFile + ok notify', async () => {
  reset();
  let got = null;
  const app = makeApp({ applyProjectFile: async (proj) => { got = proj; return proj.name; } });
  await new ExportService(app).openProjectFile(VALID_STENCIL('Plan'));
  assert.equal(got.name, 'Plan');
  assert.deepEqual(lastNote(), ['Opened project “Plan”', 'ok']);
});

test('openProjectFile: unreadable File (text() throws) → read-error notify', async () => {
  reset();
  const badFile = { text: async () => { throw new Error('io'); } };
  await new ExportService(makeApp()).openProjectFile(badFile);
  assert.deepEqual(lastNote(), ['Could not read project file', 'fail']);
});

test('openProjectFile: applyProjectFile throwing → open-error notify', async () => {
  reset();
  const app = makeApp({ applyProjectFile: async () => { throw new Error('boom'); } });
  await new ExportService(app).openProjectFile(VALID_STENCIL('P'));
  assert.deepEqual(lastNote(), ['Could not open project: boom', 'fail']);
});

test('pickAndOpenProjectFile: FS Access opens the picked file + links for live-sync', async () => {
  reset();
  const file = { name: 'picked.stencil', text: async () => VALID_STENCIL('Picked') };
  const handle = { getFile: async () => file };
  globalThis.window = { showOpenFilePicker: async () => [handle] };
  let linked = null, applied = null;
  const app = makeApp({
    applyProjectFile: async (p) => { applied = p; return p.name; },
    stencilSync: { link: async (h, n) => { linked = [h, n]; } },
  });
  try {
    await new ExportService(app).pickAndOpenProjectFile();
    assert.equal(applied.name, 'Picked');
    assert.deepEqual(linked, [handle, 'picked.stencil']);
    assert.deepEqual(lastNote(), ['Opened project “Picked”', 'ok']);
  } finally { delete globalThis.window; }
});

test('pickAndOpenProjectFile: cancelled Open picker (AbortError) is silent', async () => {
  reset();
  globalThis.window = { showOpenFilePicker: async () => { const e = new Error('x'); e.name = 'AbortError'; throw e; } };
  try {
    await new ExportService(makeApp()).pickAndOpenProjectFile();
    assert.equal(notifications.length, 0);
  } finally { delete globalThis.window; }
});
