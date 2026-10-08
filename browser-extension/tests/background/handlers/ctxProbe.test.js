// A cold worker's first right-click: the pins cache is still empty when the probe reports, so
// the Pin ↔ Unpin relabel waits for the cache's first read instead of labelling a pinned image "Pin".
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from '../../helpers/chromeStub.js';
import { PINS_KEY } from '../../../src/lib/prefs/pins.js';
import { MSG } from '../../../src/lib/messages.js';
import { MENU, pinItemTitle } from '../../../src/lib/menu/contextMenu.js';

const PAGE = 'https://shop.example/gallery';
const IMG = 'https://cdn.example/cat.png';

test('the first probe on a cold worker relabels from the pins read, not the empty cache', async () => {
  installChromeStub({ local: { [PINS_KEY]: [{ source: IMG, site: 'https://shop.example', name: 'cat.png', kind: 'image', t: 1 }] } });
  let release;
  const held = new Promise((r) => { release = r; });
  const get = chrome.storage.local.get;
  chrome.storage.local.get = async (k) => { await held; return get(k); };
  const updates = [];
  chrome.contextMenus = { update: (id, props) => updates.push([id, props]) };
  chrome.tabs = { onRemoved: { addListener() {} } };

  const { ctxProbeHandlers } = await import('../../../src/background/handlers/ctxProbe.js');
  ctxProbeHandlers[MSG.CTX]({ data: { imgUrl: IMG } }, { tab: { id: 7, url: PAGE } });
  assert.equal(updates.some(([id, p]) => id === MENU.PIN && p.title), false, 'no label before the read lands');

  release();
  await new Promise((r) => setTimeout(r, 0));
  const titled = updates.filter(([id, p]) => id === MENU.PIN && p.title);
  assert.deepEqual(titled.map(([, p]) => p.title), [pinItemTitle(true, 'image')]);

  // Warm: the next probe relabels synchronously.
  updates.length = 0;
  ctxProbeHandlers[MSG.CTX]({ data: { imgUrl: IMG } }, { tab: { id: 7, url: PAGE } });
  assert.deepEqual(updates.filter(([id, p]) => id === MENU.PIN && p.title).map(([, p]) => p.title), [pinItemTitle(true, 'image')]);
});
