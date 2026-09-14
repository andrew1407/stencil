// Tests for the editor bridge content script (src/content/editorBridge.js). It's an
// ISOLATED-world IIFE injected on the editor origin: it reports the project registry to the
// SW, relays an "unpin" window message the editor app posts into a PAGE_PIN with pin:false
// (which the SW turns into setPinned/removePinEntry), and — for editor mode — relays in BOTH
// directions: extension → editor page (state / import / switch project, over the id-correlated
// EXT_REQ/EXT_RES postMessage channel) and editor page → SW (the stencil.extension calls that
// arrive on EXT_API). No exports — we install a stub window/localStorage/chrome on globalThis
// and import for the side effect, busting the ESM cache with a unique ?case= per scenario
// (same pattern as pageApiMain.test.js).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from './helpers/chromeStub.js';

const SRC = { EXT_REQ: 'stencil-ext-req', EXT_RES: 'stencil-ext-res', EXT_API: 'stencil-ext-api', EXT_API_RES: 'stencil-ext-api-res' };
const MSG = { EDITOR_STATE: 'stencil-editor-state', EDITOR_IMPORT: 'stencil-editor-import', EDITOR_SWITCH_PROJECT: 'stencil-editor-switch-project', EDITOR_LIST: 'stencil-editor-list' };

// `swResponse` is what chrome.runtime.sendMessage resolves with (undefined = no receiver).
const setupEnv = ({ swResponse, editorPageApi = true } = {}) => {
  const posted = [];
  const messageListeners = [];
  const win = {
    addEventListener(type, fn) { if (type === 'message') messageListeners.push(fn); },
    postMessage: (m) => posted.push(m),
  };
  // Deliver a window 'message' as the editor app's postMessage would (same-window source).
  const dispatch = (data, source = win) => { for (const fn of messageListeners) fn({ source, data }); };
  globalThis.window = win;
  globalThis.localStorage = { getItem: () => null };   // no registry → empty publishRegistry
  // The page-API relay rides on the editorPageApi setting (see editorBridge.js): the
  // bridge reads it (sync storage) on load and follows changes.
  const stub = installChromeStub({
    sync: { editorPageApi },
    respond: () => (swResponse === undefined ? undefined : Promise.resolve(swResponse)),
  });
  // Deliver a chrome.runtime message as the SW / panel would, collecting the sendResponse
  // answers and each listener's return value (true = "I'll answer asynchronously").
  const send = (msg) => {
    const replies = [];
    const kept = stub.runtimeListeners.map((fn) => fn(msg, {}, (r) => replies.push(r)));
    return { replies, kept };
  };
  return { sent: stub.sent, posted, win, dispatch, send };
};

let caseId = 0;
const loadBridge = async () => { await import(`../src/content/editorBridge.js?case=${caseId++}`); };
// Let the bridge's promise chains settle (a round-trip is a couple of microtasks).
const flush = () => new Promise((r) => setImmediate(r));
// The EXT_REQ envelopes the bridge posted at the editor page.
const pageReqs = (posted) => posted.filter((p) => p.source === SRC.EXT_REQ);
// The EXT_API_RES envelopes the bridge posted back at the MAIN-world API.
const apiRes = (posted) => posted.filter((p) => p.source === SRC.EXT_API_RES);

test('relays an unpin window message to a PAGE_PIN with pin:false', async () => {
  const { sent, dispatch } = setupEnv();
  await loadBridge();
  sent.length = 0;   // drop the initial publishRegistry message
  dispatch({
    source: 'stencil-editor-bridge', type: 'unpin',
    pinSource: 'https://img.example/a.png', resource: 'https://site.example/page',
    name: 'a', kind: 'image',
  });
  assert.equal(sent.length, 1);
  assert.deepEqual(sent[0], {
    type: 'stencil-page-pin', pin: false,
    source: 'https://img.example/a.png', resource: 'https://site.example/page',
    name: 'a', kind: 'image',
  });
});

test('ignores wrong-tag, wrong-type, and cross-window messages', async () => {
  const { sent, dispatch } = setupEnv();
  await loadBridge();
  sent.length = 0;
  dispatch({ source: 'stencil-editor-bridge', type: 'unpin', pinSource: 'x' }, {});  // not same-window
  dispatch({ source: 'other', type: 'unpin', pinSource: 'x' });                      // wrong tag
  dispatch({ type: 'unpin', pinSource: 'x' });                                       // no tag
  dispatch({ source: 'stencil-editor-bridge', type: 'registry' });                  // wrong type
  assert.equal(sent.length, 0);
});

test('EDITOR_STATE asks the page and answers with its state', async () => {
  const { posted, dispatch, send } = setupEnv();
  await loadBridge();
  const { replies, kept } = send({ type: MSG.EDITOR_STATE });
  assert.ok(kept.includes(true), 'the handler keeps the message port open');

  const req = pageReqs(posted).at(-1);
  assert.equal(req.request, 'state');
  assert.deepEqual(req.payload, { thumbnail: true });   // default; false skips the canvas capture
  assert.ok(req.id, 'the request is id-tagged');

  const state = { projectId: 'p1', projectName: 'Sketch', hasImage: true, thumbnail: 'data:,x' };
  dispatch({ source: SRC.EXT_RES, id: req.id, ok: true, result: state });
  await flush();
  assert.deepEqual(replies, [{ ok: true, state }]);
});

test('EDITOR_IMPORT and EDITOR_SWITCH_PROJECT relay verbatim and report the project', async () => {
  const { posted, dispatch, send } = setupEnv();
  await loadBridge();

  const handoff = { dataUrl: 'data:image/png;base64,AA', name: 'a.png', source: 'http://cdn/a.png' };
  const imported = send({ type: MSG.EDITOR_IMPORT, payload: handoff, mode: 'replace-keep' });
  let req = pageReqs(posted).at(-1);
  assert.equal(req.request, 'import');
  assert.deepEqual(req.payload, { handoff, mode: 'replace-keep' });
  dispatch({ source: SRC.EXT_RES, id: req.id, ok: true, result: { projectId: 'p2', projectName: 'a' } });
  await flush();
  assert.deepEqual(imported.replies, [{ ok: true, projectId: 'p2', projectName: 'a' }]);

  const switched = send({ type: MSG.EDITOR_SWITCH_PROJECT, projectId: 'gone' });
  req = pageReqs(posted).at(-1);
  assert.equal(req.request, 'switch');
  assert.deepEqual(req.payload, { projectId: 'gone' });
  // The page refuses an id it doesn't have rather than clearing the editor — pass its error on.
  dispatch({ source: SRC.EXT_RES, id: req.id, ok: false, error: 'unknown project' });
  await flush();
  assert.deepEqual(switched.replies, [{ ok: false, error: 'unknown project' }]);
});

test('an editor build with no extensionBridge never replies → the wait times out cleanly', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { send } = setupEnv();
  await loadBridge();
  const { replies } = send({ type: MSG.EDITOR_STATE });
  assert.deepEqual(replies, [], 'nothing answered yet');

  t.mock.timers.tick(1500);
  await flush();
  assert.deepEqual(replies, [{ ok: false, error: 'the editor page did not answer' }]);
});

test('a late page reply after the timeout is dropped, not answered twice', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { posted, dispatch, send } = setupEnv();
  await loadBridge();
  const { replies } = send({ type: MSG.EDITOR_STATE });
  const { id } = pageReqs(posted).at(-1);

  t.mock.timers.tick(1500);
  await flush();
  dispatch({ source: SRC.EXT_RES, id, ok: true, result: { projectId: 'p1' } });
  await flush();
  assert.equal(replies.length, 1);
  assert.equal(replies[0].ok, false);
});

test('relays a stencil.extension call to the SW and posts the response back id-correlated', async () => {
  const { sent, posted, dispatch } = setupEnv({ swResponse: { ok: true, editors: [{ tabId: 7 }] } });
  await loadBridge();
  sent.length = 0;

  dispatch({ source: SRC.EXT_API, id: 'a1', message: { type: MSG.EDITOR_LIST, thumbnails: false } });
  assert.deepEqual(sent, [{ type: MSG.EDITOR_LIST, thumbnails: false }]);
  await flush();
  assert.deepEqual(apiRes(posted), [{ source: SRC.EXT_API_RES, id: 'a1', ok: true, result: { ok: true, editors: [{ tabId: 7 }] } }]);
});

test('a refusal keeps its context (needsChoice/state) on the way back to the page API', async () => {
  const refusal = { ok: false, error: 'editor already holds an image', needsChoice: true, state: { hasImage: true } };
  const { posted, dispatch } = setupEnv({ swResponse: refusal });
  await loadBridge();

  dispatch({ source: SRC.EXT_API, id: 'a2', message: { type: MSG.EDITOR_IMPORT, image: { src: 'http://cdn/a.png' }, mode: 'ask' } });
  await flush();
  assert.deepEqual(apiRes(posted), [{ source: SRC.EXT_API_RES, id: 'a2', ok: false, error: 'editor already holds an image', result: refusal }]);
});

test('a silent service worker answers "extension is not available"', async () => {
  const { posted, dispatch } = setupEnv();   // sendMessage resolves undefined = no receiver
  await loadBridge();
  dispatch({ source: SRC.EXT_API, id: 'a3', message: { type: MSG.EDITOR_LIST } });
  await flush();
  assert.deepEqual(apiRes(posted), [{ source: SRC.EXT_API_RES, id: 'a3', ok: false, error: 'extension is not available' }]);
});

test('EDITOR_STATE with no tabId is answered locally, without waking the SW', async () => {
  const { sent, posted, dispatch } = setupEnv({ swResponse: { ok: true } });
  await loadBridge();
  sent.length = 0;

  dispatch({ source: SRC.EXT_API, id: 'a4', message: { type: MSG.EDITOR_STATE } });
  assert.deepEqual(sent, [], 'this tab’s state never leaves the page');
  const req = pageReqs(posted).at(-1);
  assert.equal(req.request, 'state');

  dispatch({ source: SRC.EXT_RES, id: req.id, ok: true, result: { projectName: 'Here' } });
  await flush();
  assert.deepEqual(apiRes(posted), [{ source: SRC.EXT_API_RES, id: 'a4', ok: true, result: { ok: true, state: { projectName: 'Here' } } }]);
});

test('the relay is a whitelist: an unlisted type is refused and never reaches the SW', async () => {
  const { sent, posted, dispatch } = setupEnv({ swResponse: { ok: true } });
  await loadBridge();
  sent.length = 0;

  dispatch({ source: SRC.EXT_API, id: 'a5', message: { type: 'stencil-page-disable' } });
  await flush();
  assert.deepEqual(sent, []);
  assert.deepEqual(apiRes(posted), [{ source: SRC.EXT_API_RES, id: 'a5', ok: false, error: 'unknown request' }]);
});

test('a call with no id is fire-and-forget: relayed, never answered', async () => {
  const { sent, posted, dispatch } = setupEnv({ swResponse: { ok: true } });
  await loadBridge();
  sent.length = 0;

  dispatch({ source: SRC.EXT_API, message: { type: 'stencil-page-open', url: 'http://cdn/a.png', newTab: true } });
  await flush();
  assert.equal(sent.length, 1);
  assert.deepEqual(apiRes(posted), []);
});

test('ignores editor-mode messages from another window', async () => {
  const { sent, posted, dispatch } = setupEnv({ swResponse: { ok: true } });
  await loadBridge();
  sent.length = 0;
  posted.length = 0;

  dispatch({ source: SRC.EXT_API, id: 'x', message: { type: MSG.EDITOR_LIST } }, {});   // cross-window
  dispatch({ source: SRC.EXT_RES, id: 'x', ok: true, result: {} }, {});
  await flush();
  assert.deepEqual(sent, []);
  assert.deepEqual(posted, []);
});

test('a second inject binds nothing twice', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });   // the probe below is never answered
  const { posted, send } = setupEnv();
  await loadBridge();
  await loadBridge();          // re-injected on the same page (navigation race / manual inject)

  const { replies } = send({ type: MSG.EDITOR_STATE });
  assert.equal(pageReqs(posted).length, 1, 'one page request, not two');
  assert.equal(replies.length, 0, 'and only one pending answer');
});

// `e.source === window` only proves same-document, so any script on the editor origin can
// post here — and some relayable types are privileged. The relay therefore rides on the same
// editorPageApi toggle as the page API it serves.
test('the page-API relay is refused when editorPageApi is off', async () => {
  const { sent, posted, dispatch } = setupEnv({ swResponse: { ok: true }, editorPageApi: false });
  await loadBridge();
  sent.length = 0;

  dispatch({ source: SRC.EXT_API, id: 'off1', message: { type: 'stencil-scan-tab', tabId: 7 } });
  await flush();
  assert.deepEqual(sent, [], 'nothing reaches the service worker');
  assert.equal(apiRes(posted).length, 1);
  assert.equal(apiRes(posted)[0].ok, false);
  assert.match(apiRes(posted)[0].error, /turned off/);
});

// The extension → editor direction is NOT gated: it is how the popup imports into an
// already-open editor.
test('editorPageApi off still lets the extension drive the page (import/state/switch)', async () => {
  const { posted, send } = setupEnv({ editorPageApi: false });
  await loadBridge();

  const { kept } = send({ type: MSG.EDITOR_STATE, thumbnail: false });
  assert.ok(kept.includes(true), 'the handler still answers asynchronously');
  assert.equal(pageReqs(posted).at(-1).request, 'state');
});
