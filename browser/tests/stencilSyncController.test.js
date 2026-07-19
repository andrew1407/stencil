// Drives the REAL StencilSync controller loop against a fake FileSystemFileHandle + app:
// auto-save writes the file, an external change applies in place, and a conflict prompts and
// resolves. Exercises the actual write/read/classify/apply paths (no browser needed).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { StencilSync } from '../js/core/stencilSync.js';

// Node has no localStorage; give the controller a tiny in-memory one so liveSync persists.
globalThis.localStorage = {
  _s: {}, getItem(k) { return this._s[k] ?? null; }, setItem(k, v) { this._s[k] = String(v); },
};

const RED = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mO4Y2T0HwAFbgJAIh+PxAAAAABJRU5ErkJggg==';

// A fake handle backed by an in-memory string; _extWrite() simulates another app editing it.
function fakeHandle(initial) {
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

// A minimal app whose project is just a line count; serialization varies with it.
function fakeApp(conflictChoice = 'theirs') {
  return {
    lineCount: 1,
    applied: [],
    conflictChoice,
    projectFileState() {
      return {
        name: 'proj', image: { dataUrl: RED, ext: 'png', w: 1, h: 1 },
        layout: { imageWidth: 1, imageHeight: 1, lines: Array.from({ length: this.lineCount }, () => ({ points: [{ x: 0, y: 0 }] })) },
      };
    },
    applyProjectFileInPlace(project, opts) { this.applied.push({ lines: project.layout.lines.length, opts }); this.lineCount = project.layout.lines.length; },
    async chooseFileConflict() { return this.conflictChoice; },
    showSaveStatus() {}, updateStencilSyncUI() {},
  };
}

test('auto-save writes the linked file after an edit', async (t) => {
  const app = fakeApp();
  const s = new StencilSync(app);
  t.after(() => s.unlink());
  const h = fakeHandle('{}');
  s.liveSync = true;
  await s.link(h, 'demo.stencil');
  app.lineCount = 3;             // the user drew two more lines…
  await s.flush();               // …auto-save pushes to the file
  assert.match(h._read(), /"format": "stencil-project"/);
  assert.equal(JSON.parse(h._read()).layout.lines.length, 3);
});

test('an external change (file edited elsewhere) is applied in place', async (t) => {
  const app = fakeApp();
  const s = new StencilSync(app);
  t.after(() => s.unlink());
  const h = fakeHandle('{}');
  s.liveSync = true;
  await s.link(h, 'demo.stencil');           // baseline = "{}" ... then seed a real doc as the file
  // Simulate another client writing a valid 2-line project.
  const external = JSON.stringify({ format: 'stencil-project', version: 1, name: 'peer',
    image: { dataUrl: RED, ext: 'png', w: 1, h: 1 },
    layout: { imageWidth: 1, imageHeight: 1, lines: [{ points: [{ x: 0, y: 0 }] }, { points: [{ x: 1, y: 1 }] }] } });
  h._extWrite(external);
  await s.check();                             // watch tick
  assert.equal(app.applied.length, 1, 'external change applied once');
  assert.equal(app.applied[0].lines, 2);
  assert.equal(app.applied[0].opts.mergeLines ?? false, false);
});

test('a conflict (both changed) prompts and, on "theirs", reloads the file', async (t) => {
  const app = fakeApp('theirs');
  const s = new StencilSync(app);
  t.after(() => s.unlink());
  const h = fakeHandle('{}');
  s.liveSync = true;
  await s.link(h, 'demo.stencil');
  app.lineCount = 5;                           // un-synced local edits…
  const external = JSON.stringify({ format: 'stencil-project', version: 1, name: 'peer',
    image: { dataUrl: RED, ext: 'png', w: 1, h: 1 },
    layout: { imageWidth: 1, imageHeight: 1, lines: [{ points: [{ x: 0, y: 0 }] }] } });
  h._extWrite(external);                        // …and the file also changed → conflict
  await s.check();
  assert.equal(app.applied.length, 1, 'took theirs');
  assert.equal(app.applied[0].lines, 1);
});

test('merge choice unions lines and writes the merged result back', async (t) => {
  const app = fakeApp('merge');
  const s = new StencilSync(app);
  t.after(() => s.unlink());
  const h = fakeHandle('{}');
  s.liveSync = true;
  await s.link(h, 'demo.stencil');
  app.lineCount = 4;
  const external = JSON.stringify({ format: 'stencil-project', version: 1, name: 'peer',
    image: { dataUrl: RED, ext: 'png', w: 1, h: 1 },
    layout: { imageWidth: 1, imageHeight: 1, lines: [{ points: [{ x: 9, y: 9 }] }] } });
  h._extWrite(external);
  await s.check();
  assert.equal(app.applied[0].opts.mergeLines, true, 'merge requested');
  // after merge the controller writes the merged current state back to the file
  assert.match(h._read(), /"format": "stencil-project"/);
});
