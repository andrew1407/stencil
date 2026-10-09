// js/core/image/loadFlow.js: the newest of overlapping loads wins whatever order the decodes land
// in, and a .stencil link made after the call (open-and-link) survives the decode's unlink.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

installDom().register('notify-balloon', createStubElement('div', { notify: () => {} }));
const { loadImageFromFile } = await import('../../../js/core/image/loadFlow.js');

// Each decode waits until the test releases it, by file name, so the order is the test's.
const pending = new Map();
globalThis.FileReader = class {
  readAsDataURL(file) { queueMicrotask(() => this.onload({ target: { result: `data:${file.name}` } })); }
};
globalThis.Image = class {
  get naturalWidth() { return 10; }
  get naturalHeight() { return 10; }
  set src(v) { pending.set(v.slice(5), () => this.onload()); }
};
const tick = () => new Promise((r) => setTimeout(r, 0));
const release = async (name) => { pending.get(name)(); await tick(); };

const makeApp = () => ({
  lines: [], activeProjectId: 'p1', storage: { temporary: false, incognito: false },
  stencilSync: { handle: null, unlink() { this.handle = null; } },
});

test('overlapping loads: the newest wins even when the older picture decodes last', async () => {
  const app = makeApp();
  loadImageFromFile(app, { name: 'first.png' }, { replaceInPlace: true });
  loadImageFromFile(app, { name: 'second.png' }, { replaceInPlace: true });
  await tick();
  await release('second.png');
  assert.equal(app.imageBaseName, 'second');
  const newest = app.originalImage;
  await release('first.png');
  assert.equal(app.imageBaseName, 'second', 'the stale decode does no bookkeeping');
  assert.equal(app.originalImage, newest, 'and never becomes the original');
  assert.equal(app.imageDataUrl, 'data:second.png');
});

test('a fresh load drops the previous file link, but keeps one made after the call', async () => {
  const app = makeApp();
  app.stencilSync.handle = 'old.stencil';
  loadImageFromFile(app, { name: 'a.png' });
  await tick();
  await release('a.png');
  assert.equal(app.stencilSync.handle, null, 'the old file never receives the new project');
  loadImageFromFile(app, { name: 'b.png' });
  app.stencilSync.handle = 'opened.stencil';
  await tick();
  await release('b.png');
  assert.equal(app.stencilSync.handle, 'opened.stencil');
});
