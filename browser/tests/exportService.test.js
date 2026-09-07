// Unit tests for ExportService (js/core/exportService.js) — the export/clipboard/file-IO
// cluster extracted out of DrawingApp. The service holds no state; it reads a back-referenced
// `app` and routes mutations through the app's shared methods. We drive it with a minimal stub
// app + a notify spy (notify() posts to a #notify-balloon element if present) and assert the
// guard branches and the shared #applyValidatedLayout routing used by upload + paste.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// notify() (utils.js) looks up #notify-balloon and calls its .notify(msg, type); spy on it.
import { installDom } from './helpers/dom.js';

const notifications = [];
const balloon = { notify: (msg, type) => notifications.push([msg, type]) };
installDom().register('notify-balloon', balloon);

const { ExportService } = await import('../js/core/exportService.js');

// A mock app carrying just what the driven paths touch. `record` collects calls to the
// shared app methods so we can assert the service delegates instead of reimplementing.
const makeApp = (over = {}) => {
  const record = { saveHistory: 0, redraw: 0, updateButtons: 0, coordUpdate: [] };
  const app = {
    record,
    image: null,
    lines: [],
    canvas: { width: 100, height: 80 },
    confirm: async () => true,
    askAlt: async () => 'confirm',
    saveHistory() { record.saveHistory++; },
    renderer: { redraw() { record.redraw++; } },
    strokeFx: { suspend() {}, resume() {} },
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

test('saveImage("split"): outside a split compare view → fail notify, no work', () => {
  reset();
  new ExportService(makeApp({ image: {}, compareMode: 'none' })).saveImage('split');
  assert.deepEqual(lastNote(), ['Turn on split compare to download with the splitter', 'fail']);
});

// ── Export variants (js/core/exportService.js renderExportCanvas) ──
// The stub canvas's getContext() returns null (helpers/dom.js) — fine for 'current'/'tint'/
// the split render, which only ever reach the CTX through app.renderer's own methods (mocked
// below). Only the 'original' branch touches ctx directly, so it gets a real stand-in.
const fakeCtx = () => ({
  filter: 'none', drawImage() {}, save() {}, restore() {}, beginPath() {}, rect() {}, clip() {},
});
// Swap document.createElement for the duration of `fn`, faking 'canvas' (a working 2D
// ctx stand-in + toBlob/toDataURL) and 'a' (a clickable download link) — the two elements
// saveImage/copyImageToClipboard actually create. Returns fn's result plus every <a> made,
// so a test can inspect the .download filename it was given.
const withFakeCanvas = (fn) => {
  const orig = document.createElement;
  const ctx = fakeCtx();
  const links = [];
  document.createElement = (tag) => {
    if (tag === 'canvas') {
      return { width: 0, height: 0, getContext: () => ctx, toDataURL: () => 'data:image/png;base64,x', toBlob: (cb) => cb(new Blob()) };
    }
    if (tag === 'a') { const a = { click() {} }; links.push(a); return a; }
    return orig(tag);
  };
  try { return { result: fn(ctx), links }; } finally { document.createElement = orig; }
};

test('renderExportCanvas: "current" draws the filter + the visible lines, not points', () => {
  const calls = [];
  const app = makeApp({
    image: {}, showLines: true, showPoints: false, lines: [{ points: [{ x: 0, y: 0 }], color: '#000' }],
    renderer: {
      drawImageWithFilter: () => calls.push('filter'),
      drawLine: (line, sel) => calls.push(['line', sel]),
      drawPoint: () => calls.push('point'),
    },
  });
  const off = new ExportService(app).renderExportCanvas('current');
  assert.equal(off.width, app.canvas.width);
  assert.deepEqual(calls, ['filter', ['line', false]]);
});

test('renderExportCanvas: "tint" draws the filter but never annotations', () => {
  const calls = [];
  const app = makeApp({
    image: {}, showLines: true, showPoints: true, lines: [{ points: [{ x: 0, y: 0 }], color: '#000' }],
    renderer: { drawImageWithFilter: () => calls.push('filter'), drawLine: () => calls.push('line'), drawPoint: () => calls.push('point') },
  });
  new ExportService(app).renderExportCanvas('tint');
  assert.deepEqual(calls, ['filter']);
});

test('renderExportCanvas: "original" draws the raw image only — no filter, no annotations', () => {
  const calls = [];
  const app = makeApp({
    image: {}, showLines: true, lines: [{ points: [{ x: 0, y: 0 }], color: '#000' }],
    renderer: { drawImageWithFilter: () => calls.push('filter'), drawLine: () => calls.push('line') },
  });
  withFakeCanvas(() => new ExportService(app).renderExportCanvas('original'));
  assert.deepEqual(calls, []);
});

test('renderExportCanvas("split"): draws the filtered+annotated frame, then a CLEAN split — no divider, no knob', () => {
  const calls = [];
  const app = makeApp({
    image: {}, showLines: true, compareMode: 'vertical', lines: [{ points: [{ x: 0, y: 0 }], color: '#000' }],
    renderer: {
      drawImageWithFilter: () => calls.push('filter'),
      drawLine: () => calls.push('line'),
      drawCompareSplit: (mode, opts) => calls.push(['split', mode, opts]),
    },
  });
  new ExportService(app).renderExportCanvas('split');
  assert.deepEqual(calls, ['filter', 'line', ['split', 'vertical', { withDivider: false }]]);
});

test('renderExportCanvas("split"): "horizontal" compare mode passes through as-is', () => {
  const calls = [];
  const app = makeApp({
    image: {}, compareMode: 'horizontal',
    renderer: { drawImageWithFilter() {}, drawCompareSplit: (mode) => calls.push(mode) },
  });
  new ExportService(app).renderExportCanvas('split');
  assert.deepEqual(calls, ['horizontal']);
});

test('saveImage: each variant downloads with its own filename suffix', () => {
  const app = makeApp({
    image: {}, imageBaseName: 'pic', imageExt: 'png',
    renderer: { drawImageWithFilter() {}, drawLine() {}, drawPoint() {} },
  });
  const svc = new ExportService(app);
  const { links } = withFakeCanvas(() => {
    svc.saveImage('current'); svc.saveImage('original'); svc.saveImage('tint');
  });
  assert.deepEqual(links.map(l => l.download), ['pic-drawing.png', 'pic-drawing-original.png', 'pic-drawing-tint.png']);
});

test('saveImage("split"): downloads a CLEAN split composite while a split compare view is active — no divider/knob baked in', () => {
  const calls = [];
  const app = makeApp({
    image: {}, compareMode: 'vertical', imageBaseName: 'pic', imageExt: 'png',
    renderer: { drawImageWithFilter() {}, drawCompareSplit: (mode, opts) => calls.push([mode, opts]) },
  });
  const { links } = withFakeCanvas(() => new ExportService(app).saveImage('split'));
  assert.deepEqual(links.map(l => l.download), ['pic-drawing-split.png']);
  assert.deepEqual(calls, [['vertical', { withDivider: false }]]);
});

// ── copyImageToClipboard: variant labels + the split-compare special case ──
const fakeOffscreen = () => ({ toBlob: (cb) => cb(new Blob()) });
const withFakeClipboard = async (fn) => {
  // Node's own `navigator` global is a getter-only accessor — redefine it, don't assign.
  const origNav = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
  const origCI = globalThis.ClipboardItem;
  Object.defineProperty(globalThis, 'navigator', {
    configurable: true, value: { clipboard: { write: () => Promise.resolve() } },
  });
  globalThis.ClipboardItem = function (parts) { this.parts = parts; };
  try { return await fn(); } finally {
    if (origNav) Object.defineProperty(globalThis, 'navigator', origNav); else delete globalThis.navigator;
    globalThis.ClipboardItem = origCI;
  }
};

test('copyImageToClipboard: "current" ignores compare mode — always the plain edited frame, never auto-upgraded to split', async () => {
  // The PRIMARY-gesture decision (split vs current) is made by the CALLER
  // (controlsBinder.js's copyImage hotkey handler, exportOptionsMenu.js's openFull) —
  // this method never substitutes one for the other, so an explicit 'current' request
  // (the menu's own "Current" row, reachable even while comparing) stays literal.
  reset();
  const app = makeApp({ image: {}, compareMode: 'horizontal' });
  const svc = new ExportService(app);
  const seen = [];
  svc.renderExportCanvas = (v) => { seen.push(v); return fakeOffscreen(); };
  await withFakeClipboard(() => svc.copyImageToClipboard('current'));
  assert.deepEqual(seen, ['current']);
  assert.deepEqual(lastNote(), ['Image copied to clipboard', 'ok']);
});

test('copyImageToClipboard: "split" is its own explicit variant, valid only while comparing', async () => {
  reset();
  const app = makeApp({ image: {}, compareMode: 'horizontal' });
  const svc = new ExportService(app);
  const seen = [];
  svc.renderExportCanvas = (v) => { seen.push(v); return fakeOffscreen(); };
  await withFakeClipboard(() => svc.copyImageToClipboard('split'));
  assert.deepEqual(seen, ['split']);
  assert.deepEqual(lastNote(), ['Split image copied to clipboard', 'ok']);
});

test('copyImageToClipboard: "original"/"tint" ignore compare mode — always the plain render', async () => {
  reset();
  const app = makeApp({ image: {}, compareMode: 'vertical' });
  const svc = new ExportService(app);
  const seen = [];
  svc.renderExportCanvas = (v) => { seen.push(v); return fakeOffscreen(); };
  await withFakeClipboard(async () => {
    await svc.copyImageToClipboard('original');
    await svc.copyImageToClipboard('tint');
  });
  assert.deepEqual(seen, ['original', 'tint']);
  assert.deepEqual(notifications.map(n => n[0]), ['Original image copied to clipboard', 'Tinted image copied to clipboard']);
});

test('copyImageToClipboard: "current" with no split compare active is the plain render, default label', async () => {
  reset();
  const app = makeApp({ image: {}, compareMode: 'none' });
  const svc = new ExportService(app);
  const seen = [];
  svc.renderExportCanvas = (v) => { seen.push(v); return fakeOffscreen(); };
  await withFakeClipboard(() => svc.copyImageToClipboard());
  assert.deepEqual(seen, ['current']);
  assert.deepEqual(lastNote(), ['Image copied to clipboard', 'ok']);
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

test('applyPastedLayout over existing lines: Cancel → canceled, no mutation', async () => {
  reset();
  // Existing lines + a valid payload raises the Combine/Replace/Cancel prompt; back out.
  const app = makeApp({ image: {}, lines: [{ points: [{ x: 0, y: 0 }] }], askAlt: async () => null });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] });
  assert.deepEqual(lastNote(), ['Layout paste canceled', 'info']);
  assert.equal(app.record.saveHistory, 0);
});

test('the layout prompt gives each real answer its own glyph, not a generic tick', async () => {
  reset();
  let opts = null;
  const app = makeApp({
    image: {}, lines: [{ points: [{ x: 0, y: 0 }] }],
    askAlt: async (_msg, o) => { opts = o; return null; },
  });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] });
  assert.equal(opts.confirmLabel, 'Replace');
  assert.equal(opts.altLabel, 'Combine');
  // Replace swaps one layout for the other; Combine stacks them. A check mark would
  // say nothing about either, and a bare word beside two icon buttons reads as odd.
  assert.equal(opts.confirmIcon, 'swap');
  assert.equal(opts.altIcon, 'layers');
  const { ICONS } = await import('../js/ui/icons.js');
  assert.ok(ICONS[opts.confirmIcon], 'confirmIcon names a real glyph');
  assert.ok(ICONS[opts.altIcon], 'altIcon names a real glyph');
});

test('applyPastedLayout over existing lines: Replace drops the old ones', async () => {
  reset();
  const app = makeApp({ image: {}, lines: [{ points: [{ x: 0, y: 0 }] }], askAlt: async () => 'confirm' });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] });
  assert.equal(app.lines.length, 1);
  assert.deepEqual(app.lines[0].points, [{ x: 5, y: 5 }]);
  assert.deepEqual(lastNote(), ['Layout pasted from clipboard', 'ok']);
});

test('applyPastedLayout over existing lines: Combine keeps them and adds the new on top', async () => {
  reset();
  const app = makeApp({ image: {}, lines: [{ points: [{ x: 0, y: 0 }] }], askAlt: async () => 'alt' });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] });
  assert.equal(app.lines.length, 2, 'the existing line survives');
  assert.deepEqual(app.lines[0].points, [{ x: 0, y: 0 }]);   // old first…
  assert.deepEqual(app.lines[1].points, [{ x: 5, y: 5 }]);   // …new on top
  assert.deepEqual(lastNote(), ['Layout pasted from clipboard (combined)', 'ok']);
});

test('installLayout: silent, and mode:"combine" appends instead of replacing', async () => {
  reset();
  const app = makeApp({ image: {}, lines: [{ points: [{ x: 0, y: 0 }] }] });
  const svc = new ExportService(app);
  // No prompt is consulted and nothing is announced — this is the programmatic path.
  app.askAlt = () => { throw new Error('installLayout must never prompt'); };
  assert.equal(svc.installLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] }, { mode: 'combine' }), true);
  assert.equal(app.lines.length, 2);
  assert.equal(lastNote(), undefined, 'no toast');
  // Default mode replaces.
  svc.installLayout({ lines: [{ points: [{ x: 9, y: 9 }] }] });
  assert.equal(app.lines.length, 1);
  // history:false keeps it out of undo.
  const before = app.record.saveHistory;
  svc.installLayout({ lines: [{ points: [{ x: 1, y: 1 }] }] }, { history: false });
  assert.equal(app.record.saveHistory, before);
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
