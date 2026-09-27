// Drives the REAL StencilSync controller loop against a stub FileSystemFileHandle + app:
// auto-save writes the file, an external change applies in place, and a conflict prompts and
// resolves. Exercises the actual write/read/classify/apply paths (no browser needed).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { StencilSync } from '../../../js/core/remote/stencilSync.js';
import { installDom, createStubElement } from '../../helpers/dom.js';

// Node has no localStorage; give the controller an in-memory one so liveSync persists.
import { installMemoryStorage } from '../../helpers/memoryStorage.js';

installMemoryStorage();
// The live-sync button the real updateStencilSyncUI paints.
const liveSyncBtn = installDom().register('live-sync-btn', createStubElement('button'));

const RED = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mO4Y2T0HwAFbgJAIh+PxAAAAABJRU5ErkJggg==';

// a stub handle backed by an in-memory string; _extWrite() simulates another app editing it.
function stubHandle(initial) {
  let content = initial, mtime = 1;
  return {
    name: 'demo.stencil',
    async getFile() { return { text: async () => content, lastModified: mtime }; },
    async createWritable() { return { write: async (t) => { content = t; }, close: async () => { mtime++; } }; },
    async queryPermission() { return 'granted'; },
    async requestPermission() { return 'granted'; },
    _extWrite(t) { content = t; mtime++; },
    _read() { return content; },
  };
}

// A minimal app whose project is its lines: the real projectFileState serializes them, the real
// applyProjectFileInPlace replaces (or merges) them, and the real conflict prompt asks `confirm`
// — 'theirs' is the first answer yes, 'merge' the second. Every in-place apply is logged.
const localLine = (i) => ({ points: [{ x: 100 + i, y: 100 + i }] });
function stubApp(conflictChoice = 'theirs') {
  const answers = { theirs: [true], merge: [false, true], mine: [false, false] }[conflictChoice];
  const app = {
    applied: [],
    lines: [localLine(0)],
    set lineCount(n) { app.lines = Array.from({ length: n }, (_, i) => localLine(i)); },
    image: {}, canvas: { width: 1, height: 1 }, cropRect: null, rotationQuarters: 0,
    imageBaseName: 'proj', imageSource: null, imageResource: null, blankColor: '',
    imageDataUrl: RED, imageExt: 'png', originalImage: { width: 1, height: 1 },
    activeProjectId: null, theme: 'dark', accent: 'violet', customAccent: null,
    storage: { incognito: false, store: { getMeta: () => null }, saveSoon() {} },
    imageModel: { roundRect: (r) => r, rebuildCroppedImage() {} },
    remoteSync: { adoptServerFilter() {}, adoptServerFormulas() {}, adoptServerPageFormat() {} },
    history: { reset: () => app.applied.push({ lines: app.lines.map((l) => l.points[0]) }) },
    zoomPan: { fitToWindow() {} }, coordTable: { update() {} }, renderer: { redraw() {} },
    confirm: async () => answers.shift(),
    showSaveStatus() {}, updateInfo() {}, updateButtons() {},
  };
  return app;
}

// Wired as the app wires it: the live-sync button reads its controller.
const syncFor = (app) => (app.stencilSync = new StencilSync(app));

test('auto-save writes the linked file after an edit', async (t) => {
  const app = stubApp();
  const s = syncFor(app);
  t.after(() => s.unlink());
  const h = stubHandle('{}');
  s.liveSync = true;
  await s.link(h, 'demo.stencil');
  app.lineCount = 3;             // the user drew two more lines…
  await s.flush();               // …auto-save pushes to the file
  assert.match(h._read(), /"format": "stencil-project"/);
  assert.equal(JSON.parse(h._read()).layout.lines.length, 3);
  // Node has no File System Access, so the painted button is off and says why.
  assert.equal(liveSyncBtn.disabled, true);
  assert.match(liveSyncBtn.dataset.disabledReason, /Chromium/);
});

test('an external change (file edited elsewhere) is applied in place', async (t) => {
  const app = stubApp();
  const s = syncFor(app);
  t.after(() => s.unlink());
  const h = stubHandle('{}');
  s.liveSync = true;
  await s.link(h, 'demo.stencil');           // baseline = "{}" ... then seed a real doc as the file
  // Simulate another client writing a valid 2-line project.
  const external = JSON.stringify({ format: 'stencil-project', version: 1, name: 'peer',
    image: { dataUrl: RED, ext: 'png', w: 1, h: 1 },
    layout: { imageWidth: 1, imageHeight: 1, lines: [{ points: [{ x: 0, y: 0 }] }, { points: [{ x: 1, y: 1 }] }] } });
  h._extWrite(external);
  await s.check();                             // watch tick
  assert.equal(app.applied.length, 1, 'external change applied once');
  // Replaced by the file's two lines, not merged with the local one.
  assert.deepEqual(app.applied[0].lines, [{ x: 0, y: 0 }, { x: 1, y: 1 }]);
});

test('a conflict (both changed) prompts and, on "theirs", reloads the file', async (t) => {
  const app = stubApp('theirs');
  const s = syncFor(app);
  t.after(() => s.unlink());
  const h = stubHandle('{}');
  s.liveSync = true;
  await s.link(h, 'demo.stencil');
  app.lineCount = 5;                           // un-synced local edits…
  const external = JSON.stringify({ format: 'stencil-project', version: 1, name: 'peer',
    image: { dataUrl: RED, ext: 'png', w: 1, h: 1 },
    layout: { imageWidth: 1, imageHeight: 1, lines: [{ points: [{ x: 0, y: 0 }] }] } });
  h._extWrite(external);                        // …and the file also changed → conflict
  await s.check();
  assert.equal(app.applied.length, 1, 'took theirs');
  assert.deepEqual(app.applied[0].lines, [{ x: 0, y: 0 }], 'the file\'s line alone — the local five are dropped');
});

test('merge choice unions lines and writes the merged result back', async (t) => {
  const app = stubApp('merge');
  const s = syncFor(app);
  t.after(() => s.unlink());
  const h = stubHandle('{}');
  s.liveSync = true;
  await s.link(h, 'demo.stencil');
  app.lineCount = 4;
  const external = JSON.stringify({ format: 'stencil-project', version: 1, name: 'peer',
    image: { dataUrl: RED, ext: 'png', w: 1, h: 1 },
    layout: { imageWidth: 1, imageHeight: 1, lines: [{ points: [{ x: 9, y: 9 }] }] } });
  h._extWrite(external);
  await s.check();
  // Merged: the file's line first, then every local one.
  assert.deepEqual(app.applied[0].lines, [{ x: 9, y: 9 }, ...[0, 1, 2, 3].map((i) => localLine(i).points[0])], 'merge requested');
  // after merge the controller writes the merged current state back to the file
  assert.match(h._read(), /"format": "stencil-project"/);
});
