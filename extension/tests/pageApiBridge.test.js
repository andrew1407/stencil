// Tests for the page-API relay bridge (src/content/pageApiBridge.js). It's an ISOLATED-world
// IIFE that shares the page's window message bus with the MAIN-world window.stencil API and
// relays that API's action requests to the service worker via chrome.runtime.sendMessage. It's
// a trust boundary: the page is untrusted, so the bridge only relays messages that are
//   (a) same-window  (e.source === window),
//   (b) tagged by our MAIN-world API  (data.source === 'stencil-page-api'),
//   (c) carrying a message  (data.message present),
// and it handles a couple of types LOCALLY (never relaying them): PAGE_REQUEST_SYNC re-pushes
// state, PAGE_SET_FILTERS writes shared storage. Everything else is relayed verbatim.
//
// No exports — we install a fake window/chrome on globalThis and import for the side effect,
// busting the ESM cache with a unique ?case= per scenario (same pattern as editorBridge.test.js
// and pageApiMain.test.js). A fresh window per case also resets the __stencilPageBridge guard.

import { test } from 'node:test';
import assert from 'node:assert/strict';

const SRC_PAGE_API = 'stencil-page-api';
const MSG = { PAGE_REQUEST_SYNC: 'stencil-page-request-sync', PAGE_SET_FILTERS: 'stencil-page-set-filters', PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop', PAGE_PIN: 'stencil-page-pin' };
const FILTERS_KEY = 'popupFilters';

const setupEnv = () => {
  const sent = [];           // chrome.runtime.sendMessage payloads (i.e. relayed messages)
  const storageSets = [];    // chrome.storage.local.set payloads
  const messageListeners = [];
  const win = {
    addEventListener(type, fn) { if (type === 'message') messageListeners.push(fn); },
    postMessage() {},          // syncAll() pushes state via postMessage — inert here
  };
  // Deliver a window 'message'. `source` defaults to the bridge's own window (same-window).
  const dispatch = (data, source = win) => { for (const fn of messageListeners) fn({ source, data }); };
  globalThis.window = win;
  globalThis.location = { href: 'https://site.example/page' };
  globalThis.chrome = {
    runtime: { sendMessage: (m) => { sent.push(m); return Promise.resolve(); } },
    storage: {
      local: { get: async () => ({}), set: (v) => { storageSets.push(v); } },
      sync: { get: async () => ({}) },
      onChanged: { addListener() {} },
    },
  };
  return { sent, storageSets, win, dispatch };
};

let caseId = 0;
const loadBridge = async () => { await import(`../src/content/pageApiBridge.js?case=${caseId++}`); };

const relay = (type, extra = {}) => ({ source: SRC_PAGE_API, message: { type, ...extra } });

test('relays a well-formed tagged, same-window API message verbatim to the SW', async () => {
  const { sent, dispatch } = setupEnv();
  await loadBridge();
  sent.length = 0;

  const m = { type: MSG.PAGE_OPEN, url: 'https://cdn.example/a.png', name: 'a.png' };
  dispatch({ source: SRC_PAGE_API, message: m });
  assert.equal(sent.length, 1);
  assert.deepEqual(sent[0], m);   // relayed verbatim (same object contents)
});

test('drops messages that fail the source/message/window gate', async () => {
  const { sent, dispatch, win } = setupEnv();
  await loadBridge();
  sent.length = 0;

  dispatch(relay(MSG.PAGE_OPEN), {});                                  // e.source !== window (different window)
  dispatch(relay(MSG.PAGE_OPEN), { notThe: 'win' });                  // cross-window object
  dispatch({ source: 'some-other-tag', message: { type: MSG.PAGE_OPEN } });   // wrong source tag
  dispatch({ message: { type: MSG.PAGE_OPEN } });                     // absent source tag
  dispatch({ source: SRC_PAGE_API });                                // missing message
  dispatch({ source: SRC_PAGE_API, message: null });                 // null message
  dispatch(null);                                                     // no data at all
  dispatch(undefined);

  assert.equal(sent.length, 0, 'nothing relayed to the service worker');
});

test('PAGE_REQUEST_SYNC and PAGE_SET_FILTERS are handled locally, NOT relayed', async () => {
  const { sent, storageSets, dispatch } = setupEnv();
  await loadBridge();
  sent.length = 0;
  storageSets.length = 0;

  // A sync request is served locally (re-push state) — never forwarded to the SW.
  dispatch(relay(MSG.PAGE_REQUEST_SYNC));
  assert.equal(sent.length, 0);

  // A filter write goes to shared storage (which feeds the popup), NOT the SW.
  dispatch(relay(MSG.PAGE_SET_FILTERS, { filters: { minWidth: 200 } }));
  assert.equal(sent.length, 0);
  assert.equal(storageSets.length, 1);
  assert.deepEqual(storageSets[0], { [FILTERS_KEY]: { minWidth: 200 } });

  // A missing/undefined filters payload still writes an empty object (never relays).
  dispatch(relay(MSG.PAGE_SET_FILTERS));
  assert.equal(sent.length, 0);
  assert.deepEqual(storageSets[1], { [FILTERS_KEY]: {} });
});

test('other message types (PAGE_PIN, PAGE_CROP) are relayed verbatim', async () => {
  const { sent, dispatch } = setupEnv();
  await loadBridge();
  sent.length = 0;

  const pin = { type: MSG.PAGE_PIN, pin: true, url: 'https://cdn.example/b.png' };
  const crop = { type: MSG.PAGE_CROP, url: 'https://cdn.example/c.png', album: true };
  dispatch({ source: SRC_PAGE_API, message: pin });
  dispatch({ source: SRC_PAGE_API, message: crop });
  assert.deepEqual(sent, [pin, crop]);
});
