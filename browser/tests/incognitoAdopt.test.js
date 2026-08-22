// Regression tests for the chat-driven incognito openUrl (llm-contract.md §10).
//
// The bug: `openIncognito` launched a SECOND tab (`window.open` + a `#stencil=` fragment
// at the app's own URL). The browser focused it, so the user was looking at a freshly
// booted editor with an empty chat — indistinguishable from "the page reloaded and the
// conversation vanished" — while the rest of the plan kept executing back in the tab they
// could no longer see, on an editor with no image (crop threw and killed the turn).
//
// The fix adopts incognito IN PLACE, like the Open-Image dialog's "Open here" + incognito
// (and desktop's openSourceHere): flush the outgoing project, reset the editor KEEPING the
// live conversation, switch incognito on, load here.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom } from './helpers/dom.js';

installDom({}, {
  location: { hash: '', pathname: '/', search: '' },
  history: { replaceState: () => {} },
});

const { DrawingApp } = await import('../js/core/drawingApp.js');

const src = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');

const makeMock = (over = {}) => {
  const calls = [];
  return {
    calls,
    storage: { incognito: false, save() { calls.push(['save']); } },
    newEditor(opts) { calls.push(['newEditor', opts]); },
    updateIncognitoUI() { calls.push(['updateIncognitoUI']); },
    ...over,
  };
};

const adopt = (mock) => DrawingApp.prototype.adoptIncognitoHere.call(mock);

test('adoptIncognitoHere: flush, reset (keeping the chat), incognito on', () => {
  const mock = makeMock();
  adopt(mock);
  assert.deepEqual(mock.calls, [
    ['save'],
    ['newEditor', { keepChat: true }],
    ['updateIncognitoUI'],
  ]);
  assert.equal(mock.storage.incognito, true);
});

test('adoptIncognitoHere: an already-incognito editor has nothing to flush', () => {
  const mock = makeMock({ storage: { incognito: true, save() { mock.calls.push(['save']); } } });
  adopt(mock);
  assert.deepEqual(mock.calls.map(([n]) => n), ['newEditor', 'updateIncognitoUI']);
});

// storage.newTemporary swaps the §12 chat scope (projectOpened(null) clears the visible
// transcript AND the replay history). That is right for a project switch and fatal
// mid-turn — the adoption is the one caller that opts out.
test('newTemporary({ keepChat }) is what protects the live conversation', () => {
  const body = src('../js/core/storage.js');
  const at = body.indexOf('  newTemporary({');
  assert.ok(at > 0, 'newTemporary no longer takes options');
  const method = body.slice(at, at + 1200);
  assert.match(method, /if \(!keepChat\) this\.app\.chatPersistence\?\.projectOpened\(null\)/,
    'the chat-scope swap is no longer guarded by keepChat');
});

// The §10 removeProject fallback resets the editor the same way, mid-turn — so it takes
// the same keepChat route, and its confirm names what actually goes (there is no project).
test('the removeProject fallback clears with keepChat and an accurate confirm', () => {
  const session = src('../js/llm/chatSession.js');
  const at = session.indexOf('clearWorkingImage: async () =>');
  assert.ok(at > 0, 'the clearWorkingImage capability is gone');
  const body = session.slice(at, at + 700);
  assert.match(body, /app\.newEditor\(\{ keepChat: true \}\)/, 'the fallback wipes the conversation');
  assert.match(body, /incognito editor/, 'the incognito wording is gone');
  assert.match(body, /unsaved image and its lines/, 'the unsaved wording is gone');
  assert.match(body, /return 'removal canceled'/, 'a declined confirm no longer reports back');
});

// The load-bearing guard: a chat-driven openUrl must never navigate, reload or spawn a
// tab off the page holding the conversation.
test('the chat panel opens nothing — no window.open, no location write, no launch URL', () => {
  const session = src('../js/llm/chatSession.js');
  assert.doesNotMatch(session, /window\.open\(/, 'chatSession opens a browser tab again');
  assert.doesNotMatch(session, /buildExternalLaunchUrl/, 'chatSession builds a #stencil= launch again');
  assert.doesNotMatch(session, /location\.(assign|replace|reload|href\s*=)/, 'chatSession navigates the page');
  assert.match(session, /openIncognito: async \(url\) => \{ await window\.stencil\.load\(url, \{ incognito: true \}\); \}/,
    'openIncognito no longer adopts incognito in place');
});
