// Security/behaviour regression tests for DrawingApp.applyExternalLaunch (js/core/drawingApp.js).
// The extension hands images to the editor via the URL FRAGMENT (#stencil=<encoded JSON>);
// applyExternalLaunch consumes it. Two invariants matter here and must not silently break:
//   1. The fragment is stripped from the URL immediately (history.replaceState) so it never
//      reaches the server on a reload — "the fragment never reaches the server".
//   2. A malformed/truncated payload is caught (no throw) and reported via a fail notify.
// We also pin the dataUrl (data:) vs src (https:) fetch dispatch.
//
// applyExternalLaunch is an instance method that reaches into many collaborators AND uses
// private methods (#setExternalPage/#stripExt/#applyServerLaunch). Rather than build a whole
// DrawingApp, we invoke the real method via `.call(mockApp)` with a minimal fake `this` and
// stubbed globals (location/history/fetch/document). We deliberately drive only the payload
// shapes that DON'T reach a private method: no `page` (would hit #setExternalPage), no
// `server` kind (#applyServerLaunch), no `open:'resume'` and no source-on-a-persistent-editor
// (both hit #stripExt). Those paths need a real class instance and are out of scope for a unit
// test; what we lock in is the fragment-strip, the malformed-JSON guard, and the fetch routing.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// notify() (utils.js) posts to a #notify-balloon element if present; expose one so we can spy.
const notifications = [];
const balloon = { notify: (msg, type) => notifications.push([msg, type]) };

// Mutable fake globals the method reads/writes. Reset per test via resetGlobals().
let replaceStateCalls = [];
let fetchCalls = [];
let fetchImpl = () => Promise.resolve({ ok: true, blob: async () => ({ type: 'image/png' }) });

globalThis.document = {
  getElementById: (id) => (id === 'notify-balloon' ? balloon : null),
};
globalThis.location = { hash: '', pathname: '/app', search: '' };
globalThis.history = { replaceState: (...args) => replaceStateCalls.push(args) };
globalThis.fetch = (...args) => { fetchCalls.push(args); return fetchImpl(...args); };

const { DrawingApp } = await import('../js/core/drawingApp.js');

const resetGlobals = () => {
  notifications.length = 0;
  replaceStateCalls = [];
  fetchCalls = [];
  fetchImpl = () => Promise.resolve({ ok: true, blob: async () => ({ type: 'image/png' }) });
  globalThis.location = { hash: '', pathname: '/app', search: '' };
  globalThis.history = { replaceState: (...args) => replaceStateCalls.push(args) };
};

// The minimum `this` applyExternalLaunch touches on the non-private paths we drive.
const makeMock = (over = {}) => {
  const loaded = [];
  return {
    loaded,
    storage: { incognito: false, store: {} },
    loadImageFromFile: (...args) => loaded.push(args),
    updateIncognitoUI() {},
    ...over,
  };
};

// Encode a payload the way the extension does: #stencil=<encodeURIComponent(JSON)>.
const fragmentFor = (payload) => '#stencil=' + encodeURIComponent(JSON.stringify(payload));
const run = (mock) => DrawingApp.prototype.applyExternalLaunch.call(mock);

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
  assert.equal(mock.loaded.length, 0);
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
  assert.equal(opts, undefined);                 // data: URLs are fetched with no options (no CORS mode)
  assert.equal(mock.loaded.length, 1);           // loadImageFromFile(file, opts) was reached
  const [file] = mock.loaded[0];
  assert.equal(file.name, 'shared.png');
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
  assert.deepEqual(opts, { mode: 'cors' });      // remote image → cross-origin fetch
  assert.equal(mock.loaded.length, 1);
});

test('a failed fetch is caught and reported as a fail (no throw escapes)', async () => {
  resetGlobals();
  fetchImpl = () => Promise.resolve({ ok: false, status: 404, blob: async () => ({ type: 'image/png' }) });
  globalThis.location.hash = fragmentFor({ src: 'https://cdn.example/missing.png', name: 'm.png' });
  const mock = makeMock();
  assert.doesNotThrow(() => run(mock));
  await new Promise((r) => setTimeout(r, 0));

  assert.equal(mock.loaded.length, 0);
  assert.deepEqual(notifications.at(-1), ['Stencil: failed to load the shared image', 'fail']);
});
