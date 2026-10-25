// Tests for the page-API relay bridge (src/content/pageApiBridge.js) — an ISOLATED-world IIFE
// sharing the page's message bus with the MAIN-world window.stencil API and relaying its requests
// to the service worker. It is a trust boundary: only same-window messages tagged
// `stencil-page-api` and carrying a message are relayed, while PAGE_REQUEST_SYNC and
// PAGE_SET_FILTERS are handled locally. No exports — stubs on globalThis, a unique ?case= per run.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from '../helpers/chromeStub.js';

const SRC_PAGE_API = 'stencil-page-api';
const MSG = { PAGE_REQUEST_SYNC: 'stencil-page-request-sync', PAGE_SET_FILTERS: 'stencil-page-set-filters', PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop', PAGE_PIN: 'stencil-page-pin' };
const FILTERS_KEY = 'popupFilters';
const DEFAULTS = {
  search: '', regex: false, minW: null, maxW: null, minH: null, maxH: null,
  includeImg: true, includeBg: true, includeVideo: true, includePosters: true, includeMeta: true, disabledFormats: [],
};

const setupEnv = () => {
  const listeners = {};
  const win = {
    addEventListener(type, fn) { (listeners[type] ||= []).push(fn); },
    removeEventListener(type, fn) { listeners[type] = (listeners[type] || []).filter((f) => f !== fn); },
    dispatchEvent(ev) { for (const fn of [...(listeners[ev.type] || [])]) fn(ev); return true; },
    postMessage() {},          // syncAll() pushes state via postMessage — inert here
  };
  // Deliver a window 'message'. `source` defaults to the bridge's own window (same-window).
  const dispatch = (data, source = win) => { for (const fn of [...(listeners.message || [])]) fn({ source, data }); };
  globalThis.window = win;
  globalThis.location = { href: 'https://site.example/page' };
  const stub = installChromeStub();
  // sent = runtime.sendMessage payloads (relayed messages); storageSets = local.set payloads.
  return { sent: stub.sent, storageSets: stub.sets, win, dispatch, listeners };
};

let caseId = 0;
const loadBridge = async () => {
  const id = caseId++;
  await import(`../../src/content/orphan.js?case=${id}`);   // registered ahead of the bridge
  await import(`../../src/content/pageApiBridge.js?case=${id}`);
};

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

  // A filter write goes to shared storage (which feeds the popup), NOT the SW — in the popup's
  // shape alone, whatever else the page put in it.
  dispatch(relay(MSG.PAGE_SET_FILTERS, { filters: { minW: 200, search: 'cat', junk: { deep: 1 } } }));
  assert.equal(sent.length, 0);
  assert.equal(storageSets.length, 1);
  assert.deepEqual(storageSets[0], { [FILTERS_KEY]: { ...DEFAULTS, minW: 200, search: 'cat' } });

  // A missing/undefined filters payload writes the defaults (never relays).
  dispatch(relay(MSG.PAGE_SET_FILTERS));
  assert.equal(sent.length, 0);
  assert.deepEqual(storageSets[1], { [FILTERS_KEY]: DEFAULTS });
});

test('a page\'s filter write is bounded and typed; a non-object writes nothing', async () => {
  const { storageSets, dispatch } = setupEnv();
  await loadBridge();
  storageSets.length = 0;
  dispatch(relay(MSG.PAGE_SET_FILTERS, { filters: 'not an object' }));
  dispatch(relay(MSG.PAGE_SET_FILTERS, { filters: [1, 2] }));
  assert.equal(storageSets.length, 0);
  dispatch(relay(MSG.PAGE_SET_FILTERS, { filters: {
    search: 'x'.repeat(5000), regex: 'yes', minW: -4, maxW: Infinity, includeImg: false,
    disabledFormats: ['png', 'x'.repeat(100), 7, ...Array(50).fill('gif')],
  } }));
  const f = storageSets[0][FILTERS_KEY];
  assert.equal(f.search.length, 500);
  assert.equal(f.regex, false);
  assert.equal(f.minW, null);
  assert.equal(f.maxW, null);
  assert.equal(f.includeImg, false);
  assert.ok(f.disabledFormats.length <= 32 && f.disabledFormats.every((x) => typeof x === 'string' && x.length <= 16));
});

test('the opened-in-Stencil ledger a page is shown holds only its own site\'s entries', async () => {
  const posted = [];
  const { win } = setupEnv();
  win.postMessage = (m) => posted.push(m);
  globalThis.chrome.storage.local.get = async () => ({ 'stencil-opened': [
    { source: 'https://cdn.example/mine.png', resource: 'https://site.example/a' },
    { source: 'https://other.example/theirs.png', resource: 'https://bank.example/statement' },
  ] });
  await loadBridge();
  await new Promise((r) => setTimeout(r, 0));
  const edited = posted.find((m) => m.source === 'stencil-page-edited');
  assert.deepEqual(edited.sources, ['https://cdn.example/mine.png']);
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

// Runs on every page browsed when the page API is on, and `e.source === window` only proves
// same-document — unrestricted, any site could post the cross-tab types.
test('the relay is a whitelist: cross-tab types from a page never reach the SW', async () => {
  const { sent, dispatch } = setupEnv();
  await loadBridge();
  sent.length = 0;

  for (const type of ['stencil-scan-tab', 'stencil-source-tabs', 'stencil-editor-list', 'stencil-editor-focus-tab', 'stencil-editor-import'])
    dispatch({ source: SRC_PAGE_API, message: { type, tabId: 7 } });
  assert.deepEqual(sent, []);

  // The page API's own surface still relays.
  dispatch({ source: SRC_PAGE_API, message: { type: MSG.PAGE_CROP, url: 'https://cdn/a.png' } });
  assert.equal(sent.length, 1);
});

test('a desktop hand-off is relayed only under a live user gesture', async () => {
  const { sent, dispatch } = setupEnv();
  await loadBridge();
  const activation = { isActive: false };
  Object.defineProperty(globalThis.navigator, 'userActivation', { value: activation, configurable: true });
  try {
    sent.length = 0;
    dispatch(relay(MSG.PAGE_OPEN, { url: 'https://cdn.example/a.png', desktop: true }));
    assert.equal(sent.length, 0, 'no gesture: dropped');
    dispatch(relay(MSG.PAGE_OPEN, { url: 'https://cdn.example/a.png' }));
    assert.equal(sent.length, 1, 'an editor open needs none');
    activation.isActive = true;
    dispatch(relay(MSG.PAGE_OPEN, { url: 'https://cdn.example/a.png', desktop: true }));
    assert.equal(sent.length, 2);
  } finally {
    delete globalThis.navigator.userActivation;
  }
});

test('an orphaned page bridge stands down at its next message; the reloaded copy binds in its place', async () => {
  const { listeners, dispatch, win, sent } = setupEnv();
  await loadBridge();
  const count = () => Object.values(listeners).reduce((n, fns) => n + fns.length, 0);
  const before = count();
  globalThis.chrome.runtime.id = undefined;
  dispatch(relay(MSG.PAGE_OPEN));
  assert.equal(count(), 0);
  assert.equal(win.__stencilPageBridge, undefined);
  globalThis.chrome.runtime.id = 'stencil-ext';
  await loadBridge();
  assert.equal(count(), before);
  sent.length = 0;
  dispatch(relay(MSG.PAGE_OPEN));
  assert.equal(sent.length, 1, 'the new copy relays');
});
