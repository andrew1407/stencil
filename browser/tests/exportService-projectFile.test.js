// The .stencil project-file paths of ExportService (js/core/exportService.js): save, open,
// pick-and-open and delete, with the live-sync link. Split from exportService.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ExportService, makeApp, notifications, reset, lastNote } from './helpers/exportServiceRig.js';
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
