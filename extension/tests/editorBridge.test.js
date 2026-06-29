// Tests for the editor bridge content script (src/content/editorBridge.js). It's an
// ISOLATED-world IIFE injected on the editor origin: it reports the project registry to the
// SW and (new) relays an "unpin" window message the editor app posts into a PAGE_PIN with
// pin:false (which the SW turns into setPinned/removePinEntry). No exports — we install a
// fake window/localStorage/chrome on globalThis and import for the side effect, busting the
// ESM cache with a unique ?case= per scenario (same pattern as pageApiMain.test.js).

import { test } from 'node:test';
import assert from 'node:assert/strict';

const setupEnv = () => {
  const sent = [];
  const messageListeners = [];
  const win = {
    addEventListener(type, fn) { if (type === 'message') messageListeners.push(fn); },
  };
  // Deliver a window 'message' as the editor app's postMessage would (same-window source).
  const dispatch = (data, source = win) => { for (const fn of messageListeners) fn({ source, data }); };
  globalThis.window = win;
  globalThis.localStorage = { getItem: () => null };   // no registry → empty publishRegistry
  globalThis.chrome = { runtime: { sendMessage: (m) => sent.push(m) } };
  return { sent, win, dispatch };
};

let caseId = 0;
const loadBridge = async () => { await import(`../src/content/editorBridge.js?case=${caseId++}`); };

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
