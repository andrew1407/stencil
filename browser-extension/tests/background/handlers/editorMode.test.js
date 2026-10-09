// The privileged editor-mode handlers (SOURCE_TABS, SCAN_TAB) from an editor PAGE: refused until
// the user sets the editor URL, answered once they have; extension pages are never gated.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from '../../helpers/chromeStub.js';
import { editorModeHandlers } from '../../../src/background/handlers/editorMode.js';
import { MSG } from '../../../src/lib/messages.js';

const EXT = 'chrome-extension://abc/';
const EDITOR_PAGE = { url: 'http://localhost:8080/', tab: { id: 1, url: 'http://localhost:8080/' } };
const PANEL = { url: `${EXT}src/popup/popup.html` };

const install = (sync) => {
  const stub = installChromeStub({ sync });
  globalThis.chrome.runtime.getURL = (p) => EXT + p;
  globalThis.chrome.tabs = {
    query: async () => [{ id: 9, url: 'https://news.example/a', title: 'A' }],
    get: async (id) => ({ id, url: 'https://news.example/a' }),
    sendMessage: async () => undefined,
  };
  globalThis.chrome.scripting = { executeScript: async () => [] };
  return stub;
};
const ask = (type, sender, msg = {}) =>
  new Promise((resolve) => editorModeHandlers[type]({ type, ...msg }, sender, resolve));

test('default settings: an editor page is refused SOURCE_TABS and SCAN_TAB', async () => {
  const stub = install({});
  try {
    for (const [type, msg] of [[MSG.SOURCE_TABS, {}], [MSG.SCAN_TAB, { tabId: 9 }]]) {
      const res = await ask(type, EDITOR_PAGE, msg);
      assert.equal(res.ok, false, type);
      assert.match(res.error, /not allowed from a page/);
    }
  } finally { stub.restore(); }
});

test('once the user sets the editor URL, the same page is answered', async () => {
  const stub = install({ editorUrl: 'http://localhost:8080/' });
  try {
    const res = await ask(MSG.SOURCE_TABS, EDITOR_PAGE);
    assert.equal(res.ok, true);
    assert.equal(res.tabs.length, 1);
    assert.equal((await ask(MSG.SCAN_TAB, EDITOR_PAGE, { tabId: 9 })).ok, true);
  } finally { stub.restore(); }
});

test('an explicit toggle-off is honoured with the URL set; extension pages are never gated', async () => {
  const stub = install({ editorUrl: 'http://localhost:8080/', editorPageApi: false });
  try {
    assert.equal((await ask(MSG.SOURCE_TABS, EDITOR_PAGE)).ok, false);
    assert.equal((await ask(MSG.SOURCE_TABS, PANEL)).ok, true);
  } finally { stub.restore(); }
});
