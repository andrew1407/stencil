// The chat-driven incognito openUrl (llm-contract.md §10) adopts incognito IN PLACE, like the Open-Image
// dialog's "Open here" and the desktop's openSourceHere: flush the outgoing project, reset the editor KEEPING
// the live conversation, switch incognito on, load here. A second tab instead leaves the user looking at a
// freshly booted editor with an empty chat while the rest of the plan executes in the tab they can no longer
// see, on an editor with no image.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { installDom } from '../../helpers/dom.js';

installDom({}, {
  location: { hash: '', pathname: '/', search: '' },
  history: { replaceState: () => {} },
});

const { adoptIncognitoHere } = await import('../../../js/core/launch/incognitoFlow.js');
const { mountStorage } = await import('../../helpers/storageRig.js');

const src = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');

const makeMock = (over = {}) => {
  const calls = [];
  return {
    calls,
    storage: {
      incognito: false,
      save() { calls.push(['save']); },
      newTemporary(opts) { calls.push(['newTemporary', opts]); this.incognito = false; },
    },
    zoomPan: { syncViewportHeight() {} },
    tabs: { reportActive() {}, reportIncognito() {} },
    updateIncognitoUI() { calls.push(['updateIncognitoUI']); },
    ...over,
  };
};

// The real newEditor (core/launch/openFlow.js) resets through storage.newTemporary, recorded here.
const adopt = (mock) => adoptIncognitoHere(mock);

test('adoptIncognitoHere: flush, reset (keeping the chat), incognito on', () => {
  const mock = makeMock();
  adopt(mock);
  assert.deepEqual(mock.calls, [
    ['save'],
    ['newTemporary', { keepChat: true }],
    ['updateIncognitoUI'],
  ]);
  assert.equal(mock.storage.incognito, true);
});

test('adoptIncognitoHere: an already-incognito editor has nothing to flush', () => {
  const mock = makeMock();
  mock.storage.incognito = true;
  adopt(mock);
  assert.deepEqual(mock.calls.map(([n]) => n), ['newTemporary', 'updateIncognitoUI']);
  assert.equal(mock.storage.incognito, true);
});

// storage.newTemporary swaps the §12 chat scope (projectOpened(null) clears the visible transcript AND the
// replay history): right for a project switch, fatal mid-turn, so the adoption is the one caller that opts out.
test('newTemporary({ keepChat }) is what protects the live conversation', (t) => {
  const { storage, chatScopes } = mountStorage(t, { image: false });
  storage.newTemporary();
  assert.deepEqual(chatScopes, [null], 'a plain reset opens the fresh, project-less chat scope');
  storage.newTemporary({ keepChat: true });
  assert.deepEqual(chatScopes, [null], 'keepChat leaves the live conversation where it is');
});

test('adoptIncognitoHere over a real Storage keeps the conversation and ends incognito', (t) => {
  const { app, chatScopes } = mountStorage(t, { image: false });
  adoptIncognitoHere(app);
  assert.deepEqual(chatScopes, [], 'the adoption swaps no chat scope');
  assert.deepEqual([app.storage.incognito, app.storage.temporary], [true, true]);
});

// The §10 removeProject fallback resets the editor the same way, mid-turn — so it takes
// the same keepChat route, and its confirm names what actually goes (there is no project).
test('the removeProject fallback clears with keepChat and an accurate confirm', async () => {
  const { projectAdapters } = await import('../../../js/llm/adapters/project.js');
  for (const [incognito, what] of [[true, 'the image in this incognito editor'],
                                   [false, 'the unsaved image and its lines']]) {
    const asked = [];
    const mock = makeMock({ image: {}, lines: [], confirm: async (msg) => { asked.push(msg); return true; } });
    mock.storage.incognito = incognito;
    assert.equal(await projectAdapters(mock).clearWorkingImage(), null);
    assert.deepEqual(asked, [`Remove ${what}? This cannot be undone.`]);
    assert.deepEqual(mock.calls.find(([n]) => n === 'newTemporary'), ['newTemporary', { keepChat: true }],
      'the fallback keeps the conversation');
  }
  const declined = makeMock({ image: {}, lines: [], confirm: async () => false });
  assert.equal(await projectAdapters(declined).clearWorkingImage(), 'removal canceled');
  assert.deepEqual(declined.calls, [], 'a declined confirm touches nothing');
});

// The load-bearing guard: a chat-driven openUrl must never navigate, reload or spawn a
// tab off the page holding the conversation.
test('the chat panel opens nothing — no window.open, no location write, no launch URL', () => {
  // The capability bag now lives in js/llm/adapters/ — guard the whole of it, not one file.
  const dir = new URL('../../../js/llm/adapters', import.meta.url);
  const session = [src('../../../js/llm/chat/session.js'),
    ...readdirSync(dir).filter((n) => n.endsWith('.js')).map((n) => src(`../../../js/llm/adapters/${n}`))].join('\n');
  assert.doesNotMatch(session, /window\.open\(/, 'the chat plumbing opens a browser tab again');
  assert.doesNotMatch(session, /buildExternalLaunchUrl/, 'the chat plumbing builds a #stencil= launch again');
  assert.doesNotMatch(session, /location\.(assign|replace|reload|href\s*=)/, 'the chat plumbing navigates the page');
  assert.match(session, /openIncognito: async \(url\) => \{ await window\.stencil\.load\(url, \{ incognito: true \}\); \}/,
    'openIncognito no longer adopts incognito in place');
});
