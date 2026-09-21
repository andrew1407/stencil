// The page → SW leg of src/content/editorBridge.js: the id-correlated relay, its type whitelist,
// what a refusal and a silent worker answer, and the editorPageApi gate over it all.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MSG, SRC, apiRes, flush, loadBridge, pageReqs, setupEnv } from '../helpers/editorBridgeEnv.js';

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

// `e.source === window` only proves same-document and some relayable types are privileged, so
// the relay rides on the same editorPageApi toggle as the page API it serves.
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
