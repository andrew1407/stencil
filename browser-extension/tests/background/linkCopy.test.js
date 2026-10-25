// "Copy Stencil link" (src/background/linkCopy.js over clipboard.js and offscreen/clipboard.js): the
// link each menu item builds from the clicked element and the share URL, the one offscreen page the
// copy goes through, and the note the page shows.
import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from '../helpers/chromeStub.js';
import { MENU } from '../../src/lib/menu/contextMenu.js';

// tabState.js listens on storage and closed tabs as it loads, so a chrome stub comes first.
const boot = installChromeStub();
globalThis.chrome.tabs = { onRemoved: { addListener() {} } };
const { lastTargetByTab } = await import('../../src/background/tabState.js');
const { copyLinkFromMenu, NOTES } = await import('../../src/background/linkCopy.js');
boot.restore();

let stub, made, notes, copyOk;
const install = (sync = {}) => {
  stub = installChromeStub({ sync, respond: (m) => Promise.resolve(m.type === 'stencil-offscreen-copy' ? { ok: copyOk } : undefined) });
  made = [];
  notes = [];
  copyOk = true;
  let open = false;
  Object.assign(globalThis.chrome.runtime, {
    getURL: (p) => `chrome-extension://id/${p}`,
    getContexts: async () => (open ? [{ documentUrl: 'chrome-extension://id/src/offscreen/clipboard.html' }] : []),
  });
  globalThis.chrome.offscreen = { createDocument: async (opts) => { made.push(opts); open = true; } };
  globalThis.chrome.scripting = { executeScript: async ({ target, args }) => { notes.push({ target, text: args[0] }); return [{}]; } };
};
const copies = () => stub.sent.filter((m) => m.type === 'stencil-offscreen-copy').map((m) => m.text);
beforeEach(() => install());
afterEach(() => { stub.restore(); lastTargetByTab.clear(); delete globalThis.chrome; });

const DEFAULT = 'https://andrew1407.github.io/stencil/';
const link = (base, src, incognito) => {
  const u = new URL(base);
  u.searchParams.set('source', src);
  if (incognito) u.searchParams.set('incognito', 'true');
  return u.href;
};

test('an image: its own src, on the published app, then the plain and the incognito link', async () => {
  const src = 'https://cdn.example.com/p.png';
  assert.equal(await copyLinkFromMenu({ menuItemId: MENU.LINK, srcUrl: src, frameId: 3 }, {}, 7), link(DEFAULT, src));
  assert.equal(await copyLinkFromMenu({ menuItemId: MENU.LINK_INCOGNITO, srcUrl: src }, {}, 7), link(DEFAULT, src, true));
  assert.deepEqual(copies(), [link(DEFAULT, src), link(DEFAULT, src, true)]);
  assert.deepEqual(notes.map((n) => n.text), [NOTES.copied, NOTES.copiedIncognito]);
  assert.deepEqual(notes[0].target, { tabId: 7, frameIds: [3] }, 'the note shows in the frame clicked');
  assert.equal(made.length, 1, 'one offscreen page serves every copy');
  assert.deepEqual(made[0].reasons, ['CLIPBOARD']);
});

test('a video carries its media URL; a background, the address its probe recorded', async () => {
  await copyLinkFromMenu({ menuItemId: MENU.FRAME_LINK, srcUrl: 'https://v.test/clip.mp4' }, {}, 1);
  lastTargetByTab.set(2, { url: 'https://site.test/hero.jpg' });
  await copyLinkFromMenu({ menuItemId: MENU.BG_LINK_INCOGNITO, srcUrl: 'https://site.test/page' }, {}, 2);
  assert.deepEqual(copies(), [link(DEFAULT, 'https://v.test/clip.mp4'), link(DEFAULT, 'https://site.test/hero.jpg', true)]);
});

test('the share URL from Options is the link\'s base', async () => {
  stub.restore();
  install({ shareUrl: 'http://localhost:8080/' });
  await copyLinkFromMenu({ menuItemId: MENU.LINK, srcUrl: 'https://h/p.png' }, {}, 1);
  assert.deepEqual(copies(), [link('http://localhost:8080/', 'https://h/p.png')]);
});

test('a source no other browser can open is never copied, and the page says why', async () => {
  for (const srcUrl of ['blob:https://v.test/9f', 'data:image/png;base64,AA']) {
    assert.equal(await copyLinkFromMenu({ menuItemId: MENU.FRAME_LINK, srcUrl }, {}, 1), null);
  }
  assert.deepEqual(copies(), []);
  assert.deepEqual(made, [], 'no offscreen page for nothing to copy');
  assert.deepEqual(notes.map((n) => n.text), [NOTES.unshareable, NOTES.unshareable]);
});

test('a refused copy says so; a page the extension cannot script still copies', async () => {
  copyOk = false;
  assert.equal(await copyLinkFromMenu({ menuItemId: MENU.LINK, srcUrl: 'https://h/p.png' }, {}, 1), null);
  assert.equal(notes.at(-1).text, NOTES.failed);
  copyOk = true;
  globalThis.chrome.scripting.executeScript = async () => { throw new Error('Cannot access a chrome:// URL'); };
  assert.ok(await copyLinkFromMenu({ menuItemId: MENU.LINK, srcUrl: 'https://h/p.png' }, {}, 1));
});

test('the offscreen page copies the text it is sent, and ignores every other message', async () => {
  const listeners = [];
  globalThis.chrome.runtime.onMessage = { addListener: (fn) => listeners.push(fn) };
  const area = { value: '', select() { this.selected = true; } };
  const ran = [];
  globalThis.document = { getElementById: (id) => (id === 'text' ? area : null), execCommand: (c) => { ran.push([c, area.value]); return true; } };
  try {
    await import(`../../src/offscreen/clipboard.js?t=${Date.now()}`);
    const replies = [];
    assert.equal(listeners[0]({ type: 'stencil-open-tab' }, {}, (r) => replies.push(r)), false);
    listeners[0]({ type: 'stencil-offscreen-copy', text: 'https://a/?source=b' }, {}, (r) => replies.push(r));
    assert.deepEqual(ran, [['copy', 'https://a/?source=b']]);
    assert.deepEqual(replies, [{ ok: true }]);
    assert.ok(area.selected);
  } finally {
    delete globalThis.document;
  }
});
