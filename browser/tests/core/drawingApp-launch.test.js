// applyExternalLaunch (js/core/launch/controller.js) — the extension's `#stencil=` hand-off,
// driven through a stub app down to the real load and settle. Pinned: the fragment is
// stripped at once (history.replaceState) so it never reaches the server, a malformed payload
// notifies instead of throwing, data: vs https: fetch dispatch, and an import into a live editor
// starts a new project for 'new' while a replace leaves the target's page format alone.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../helpers/dom.js';
import { installFetchStub } from '../helpers/fetchStub.js';

// notify() (utils.js) posts to a #notify-balloon element if present; expose one so we can spy.
const notifications = [];
const balloon = { notify: (msg, type) => notifications.push([msg, type]) };

// Mutable stub globals the method reads/writes. Reset per test via resetGlobals().
let replaceStateCalls = [];
let fetchImpl = () => Promise.resolve({ ok: true, blob: async () => ({ type: 'image/png' }) });

installDom({}, {
  location: { hash: '', pathname: '/app', search: '' },
  history: { replaceState: (...args) => replaceStateCalls.push(args) },
}).register('notify-balloon', balloon);
// The stub delegates so a test can swap `fetchImpl` partway; calls land on fetchStub.calls.
const fetchStub = installFetchStub((...args) => fetchImpl(...args));
const fetchCalls = fetchStub.calls;

const { applyExternalLaunch } = await import('../../js/core/launch/controller.js');

// The decode boundary: the load reads its file and the picture decodes at once, so the real
// loadImageFromFile and settleLoadedImage run against the mock. `reads` is every file loaded.
const reads = [];
globalThis.FileReader = class {
  readAsDataURL(file) {
    reads.push(file);
    queueMicrotask(() => this.onload?.({ target: { result: 'data:image/png;base64,AAAA' } }));
  }
};
globalThis.Image = class {
  width = 40; height = 30;
  set src(v) { this.url = v; queueMicrotask(() => this.onload?.()); }
  get src() { return this.url; }
};
const FULL_FRAME = { x: 0, y: 0, width: 40, height: 30 };
const DEFAULT_CROP = { x: 5, y: 0, width: 30, height: 30 };

const resetGlobals = () => {
  notifications.length = 0;
  reads.length = 0;
  replaceStateCalls = [];
  fetchStub.reset();
  fetchImpl = () => Promise.resolve({ ok: true, blob: async () => ({ type: 'image/png' }) });
  globalThis.location = { hash: '', pathname: '/app', search: '' };
  globalThis.history = { replaceState: (...args) => replaceStateCalls.push(args) };
};

// The minimum app applyExternalLaunch, the load and its settle touch on the paths we drive: an
// open project (so nothing is promoted), and an image model whose crop is the identity.
const makeMock = (over = {}) => ({
  storage: { incognito: false, temporary: false, store: {}, save() {} },
  activeProjectId: 'p1',
  lines: [],
  canvas: { width: 40, height: 30 },
  imageModel: {
    roundRect: (r) => ({ ...r }),
    rotatedOriginalDims: () => ({ width: 40, height: 30 }),
    defaultCropRect: () => DEFAULT_CROP,
    rebuildCroppedImage() {},
  },
  history: { reset() {} },
  zoomPan: { fitToWindow() {} },
  coordTable: { update() {} }, hideSelectionPanels() {},
  renderer: { redraw() {}, layers: () => [] },
  tabs: { reportActive() {}, reportIncognito() {} },
  updateInfo() {}, updateButtons() {}, updateCoordStatus() {}, updateIncognitoUI() {},
  ...over,
});

// Encode a payload the way the extension does: #stencil=<encodeURIComponent(JSON)>.
const fragmentFor = (payload) => '#stencil=' + encodeURIComponent(JSON.stringify(payload));
const run = (mock) => applyExternalLaunch(mock);

test('no #stencil= fragment → the method is a no-op (no URL rewrite, no fetch)', () => {
  resetGlobals();
  globalThis.location.hash = '#something-else';
  run(makeMock());
  assert.equal(replaceStateCalls.length, 0);
  assert.equal(fetchCalls.length, 0);
});

test('a valid fragment is stripped from the URL immediately (never reaches the server)', async () => {
  resetGlobals();
  globalThis.location = { hash: fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', name: 'a.png' }), pathname: '/editor', search: '?q=1' };
  globalThis.history = { replaceState: (...args) => replaceStateCalls.push(args) };
  const mock = makeMock();
  run(mock);

  // history.replaceState(null, '', pathname + search) — the #stencil fragment is dropped.
  assert.equal(replaceStateCalls.length, 1);
  const [state, title, url] = replaceStateCalls[0];
  assert.equal(state, null);
  assert.equal(title, '');
  assert.equal(url, '/editor?q=1');
  assert.ok(!url.includes('#stencil'), 'stripped URL carries no fragment');
});

test('a malformed/truncated #stencil= payload is caught (no throw) and reported as a fail', () => {
  resetGlobals();
  // Truncated JSON — decodeURIComponent succeeds, JSON.parse throws inside the method.
  globalThis.location.hash = '#stencil=' + encodeURIComponent('{"dataUrl":"data:image/png;base64,AAA');
  const mock = makeMock();

  assert.doesNotThrow(() => run(mock));
  // The fragment is still stripped (strip happens before the parse).
  assert.equal(replaceStateCalls.length, 1);
  // Reported via a fail notify, and nothing was fetched/loaded.
  assert.deepEqual(notifications, [['Stencil: could not read the shared image', 'fail']]);
  assert.equal(fetchCalls.length, 0);
  assert.equal(reads.length, 0);
});

test('a data: payload fetches without CORS mode and loads the decoded image', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', name: 'shared.png' });
  const mock = makeMock();
  run(mock);
  await new Promise((r) => setTimeout(r, 0));   // let the fetch().then() microtasks flush

  assert.equal(fetchCalls.length, 1);
  const [url, opts] = fetchCalls[0];
  assert.equal(url, 'data:image/png;base64,AAAA');
  assert.ok(opts.signal && opts.mode === undefined, 'data: URLs get no CORS mode — and every fetch is bounded');
  assert.equal(reads.length, 1);                 // loadImageFromFile(app, file, opts) was reached
  assert.equal(reads[0].name, 'shared.png');
  assert.equal(mock.imageBaseName, 'shared');
});

test('an https src: payload fetches with { mode: "cors" } and loads the image', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ src: 'https://cdn.example/i.png', name: 'i.png' });
  const mock = makeMock();
  run(mock);
  await new Promise((r) => setTimeout(r, 0));

  assert.equal(fetchCalls.length, 1);
  const [url, opts] = fetchCalls[0];
  assert.equal(url, 'https://cdn.example/i.png');
  assert.ok(opts.signal && opts.mode === 'cors', 'remote image → cross-origin fetch, bounded like the rest');
  assert.equal(reads.length, 1);
  assert.equal(mock.imageSource, 'https://cdn.example/i.png', 'the URL is the loaded picture\'s source');
});

test('a crop in the payload flows through to loadImageFromFile opts (open-image "new tab" + crop)', async () => {
  resetGlobals();
  const crop = { x: 10, y: 20, width: 100, height: 140 };
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', name: 'c.png', crop });
  const mock = makeMock();
  run(mock);
  await new Promise((r) => setTimeout(r, 0));

  assert.equal(reads.length, 1);
  assert.deepEqual(mock.cropRect, crop);   // the inline-crop rect rides the fragment to the loader
});

test('noCrop in the payload flows to loadImageFromFile opts (open-image "new tab", Crop off → whole frame)', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', name: 'n.png', noCrop: true });
  const mock = makeMock();
  run(mock);
  await new Promise((r) => setTimeout(r, 0));

  assert.equal(reads.length, 1);
  // Crop-off imports the full frame, not the default auto-crop.
  assert.deepEqual(mock.cropRect, FULL_FRAME);
});

test('an explicit crop wins over noCrop when both are present', async () => {
  resetGlobals();
  const crop = { x: 1, y: 2, width: 30, height: 40 };
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', name: 'b.png', crop, noCrop: true });
  const mock = makeMock();
  run(mock);
  await new Promise((r) => setTimeout(r, 0));

  assert.equal(reads.length, 1);
  assert.deepEqual(mock.cropRect, crop);   // crop present ⇒ noCrop is not forwarded
});

test('a failed fetch is caught and reported as a fail (no throw escapes)', async () => {
  resetGlobals();
  fetchImpl = () => Promise.resolve({ ok: false, status: 404, blob: async () => ({ type: 'image/png' }) });
  globalThis.location.hash = fragmentFor({ src: 'https://cdn.example/missing.png', name: 'm.png' });
  const mock = makeMock();
  assert.doesNotThrow(() => run(mock));
  await new Promise((r) => setTimeout(r, 0));

  assert.equal(reads.length, 0);
  assert.deepEqual(notifications.at(-1), ['Stencil: failed to load the shared image', 'fail']);
});
