// The editor bridge content script (src/content/editorBridge.js) relaying extension → page:
// the "unpin" window message, EDITOR_STATE/IMPORT/SWITCH_PROJECT, and the per-leg page timeout.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MSG, SRC, flush, loadBridge, pageReqs, setupEnv } from '../helpers/editorBridgeEnv.js';

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
