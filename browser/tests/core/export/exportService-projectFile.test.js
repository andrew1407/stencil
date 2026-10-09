// The .stencil project-file paths of ExportService (js/core/export/service.js, project/filePicker.js):
// save, open, pick-and-open and delete, with the live-sync link. Split from exportService.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ExportService, makeApp, notifications, reset, lastNote } from '../../helpers/exportServiceRig.js';
import { MAX_PROJECT_FILE_CHARS } from '../../../js/core/project/file.js';
// ── .stencil project file save/open ─────────────────────────────────────────
// A valid serialized project the real parseProjectFile accepts (format + v1 + embedded image).
const VALID_STENCIL = (name) => JSON.stringify({
  format: 'stencil-project', version: 1, name,
  image: { dataUrl: 'data:image/png;base64,AAAA', ext: 'png', w: 2, h: 2 }, layout: {},
});

// The session the real applyProjectFile (project/fileIO.js) opens a file into: it flushes, resets
// and loads, and the load stops at the decode, where this FileReader keeps the file it was handed.
const reads = [];
globalThis.FileReader = class { readAsDataURL(file) { reads.push(file); } };
const sessionApp = (over = {}) => {
  reads.length = 0;
  return makeApp({
    storage: { incognito: false, temporary: true, saves: 0, save() { this.saves++; }, newTemporary() {},
      promoteTemporaryToProject() {} },
    zoomPan: { syncViewportHeight() {} },
    tabs: { reportActive() {}, reportIncognito() {} },
    stencilSync: { unlink() {} },
    ...over,
  });
};

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
  const app = sessionApp();
  await new ExportService(app).openProjectFile('not a project at all');
  assert.deepEqual([app.storage.saves, reads.length], [0, 0], 'nothing is flushed or loaded');
  assert.match(lastNote()[0], /^Invalid \.stencil file: /);
  assert.equal(lastNote()[1], 'fail');
});

test('openProjectFile: valid text routes to applyProjectFile + ok notify', async () => {
  reset();
  const app = sessionApp();
  await new ExportService(app).openProjectFile(VALID_STENCIL('Plan'));
  assert.equal(app.storage.saves, 1, 'the project on screen is flushed first');
  assert.deepEqual([reads.map((f) => f.name), app.imageBaseName], [['Plan.png'], 'Plan'],
    'the parsed project is what loads, under its own name');
  assert.deepEqual(lastNote(), ['Opened project “Plan”', 'ok']);
});

test('openProjectFile: unreadable File (text() throws) → read-error notify', async () => {
  reset();
  const badFile = { text: async () => { throw new Error('io'); } };
  await new ExportService(makeApp()).openProjectFile(badFile);
  assert.deepEqual(lastNote(), ['Could not read project file', 'fail']);
});

test('openProjectFile: a File over the cap is refused by its size, never read', async () => {
  reset();
  let read = false;
  const huge = { size: MAX_PROJECT_FILE_CHARS + 1, text: async () => { read = true; return ''; } };
  await new ExportService(makeApp()).openProjectFile(huge);
  assert.equal(read, false);
  assert.deepEqual(lastNote(), ['Invalid .stencil file: Project file is too large (over 32 MiB).', 'fail']);
});

test('openProjectFile: applyProjectFile throwing → open-error notify', async () => {
  reset();
  const app = sessionApp();
  app.storage.save = () => { throw new Error('boom'); };   // the apply's own flush fails
  await new ExportService(app).openProjectFile(VALID_STENCIL('P'));
  assert.equal(reads.length, 0, 'nothing loads after the failure');
  assert.deepEqual(lastNote(), ['Could not open project: boom', 'fail']);
});

test('pickAndOpenProjectFile: FS Access opens the picked file + links for live-sync', async () => {
  reset();
  const file = { name: 'picked.stencil', text: async () => VALID_STENCIL('Picked') };
  const handle = { getFile: async () => file };
  globalThis.window = { showOpenFilePicker: async () => [handle] };
  let linked = null;
  const app = sessionApp({ stencilSync: { unlink() {}, link: async (h, n) => { linked = [h, n]; } } });
  try {
    await new ExportService(app).pickAndOpenProjectFile();
    assert.deepEqual([reads.map((f) => f.name), app.imageBaseName], [['Picked.png'], 'Picked']);
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

// ── .stencil project file delete ─────────────────────────────────────────────
// a stub StencilSync exposing just what deleteProjectFile touches: linked/handle/name + unlink().
const makeSync = (over = {}) => ({
  linked: true, name: 'plan.stencil',
  handle: { remove: async () => {} },
  unlinked: 0, unlink() { this.unlinked++; },
  ...over,
});

test('deleteProjectFile: no linked file → fail notify, nothing removed', async () => {
  reset();
  const app = makeApp({ stencilSync: makeSync({ linked: false }) });
  await new ExportService(app).deleteProjectFile();
  assert.deepEqual(lastNote(), ['No linked .stencil file to delete', 'fail']);
  assert.equal(app.stencilSync.unlinked, 0);
});

test('deleteProjectFile: handle without remove() → newer-browser fail notify', async () => {
  reset();
  const app = makeApp({ stencilSync: makeSync({ handle: {} }) });
  await new ExportService(app).deleteProjectFile();
  assert.deepEqual(lastNote(), ['Deleting files needs a newer Chromium browser', 'fail']);
  assert.equal(app.stencilSync.unlinked, 0);
});

test('deleteProjectFile: confirm declined → "Delete canceled", not removed', async () => {
  reset();
  let removed = 0;
  const sync = makeSync({ handle: { remove: async () => { removed++; } } });
  const app = makeApp({ confirm: async () => false, stencilSync: sync });
  await new ExportService(app).deleteProjectFile();
  assert.deepEqual(lastNote(), ['Delete canceled', 'info']);
  assert.equal(removed, 0);
  assert.equal(sync.unlinked, 0);
});

test('deleteProjectFile: confirmed → handle.remove() + unlink() + ok notify', async () => {
  reset();
  let removed = 0;
  const sync = makeSync({ handle: { remove: async () => { removed++; } } });
  const app = makeApp({ confirm: async () => true, stencilSync: sync });
  await new ExportService(app).deleteProjectFile();
  assert.equal(removed, 1);
  assert.equal(sync.unlinked, 1);          // link dropped so live-sync stops
  assert.deepEqual(lastNote(), ['Deleted “plan.stencil”', 'ok']);
});

test('deleteProjectFile: remove() throwing → error notify, link kept', async () => {
  reset();
  const sync = makeSync({ handle: { remove: async () => { throw new Error('locked'); } } });
  const app = makeApp({ confirm: async () => true, stencilSync: sync });
  await new ExportService(app).deleteProjectFile();
  assert.deepEqual(lastNote(), ['Could not delete file: locked', 'fail']);
  assert.equal(sync.unlinked, 0);          // failed delete leaves the project linked
});
