// The service-worker relay behind window.stencil (src/background/handlers/pageApi.js), reachable from
// any page with the API on: a non-image is never opened or cropped, the guard context is the
// sender's tab URL — never the page-supplied `resource` — and a pin records the sender's URL.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from '../../helpers/chromeStub.js';
import { MSG } from '../../../src/lib/messages.js';
import { PINS_KEY } from '../../../src/lib/prefs/pins.js';

const PAGE = 'https://site.example/gallery';
const settle = () => new Promise((r) => setTimeout(r, 20));

const rig = ({ body = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0, 0]), type = 'image/png' } = {}) => {
  const stub = installChromeStub({ sync: { editorUrl: 'https://editor.example/' } });
  const opened = [];
  const fetched = [];
  chrome.tabs = { create: async (o) => { opened.push(o); return { id: 9 }; }, onRemoved: { addListener() {} } };
  chrome.scripting = { executeScript: async (o) => { opened.push(o); } };
  chrome.runtime.getURL = (p) => `chrome-extension://ext/${p}`;
  globalThis.fetch = async (url) => {
    fetched.push(String(url));
    return new Response(body, { status: 200, headers: { 'content-type': type } });
  };
  return { stub, opened, fetched };
};

const handlers = async () => (await import('../../../src/background/handlers/pageApi.js')).pageApiHandlers;
const sender = { tab: { id: 3, url: PAGE } };

test('a caller-supplied non-image dataUrl is never opened or cropped', async () => {
  const { opened } = rig();
  const h = await handlers();
  h[MSG.PAGE_OPEN]({ dataUrl: 'data:text/html;base64,PGgxPmhpPC9oMT4=' }, sender);
  h[MSG.PAGE_CROP]({ dataUrl: 'data:text/html;base64,PGgxPmhpPC9oMT4=' }, sender);
  await settle();
  assert.deepEqual(opened, []);
});

test('a fetched body that is not an image never reaches the editor, whatever the URL says', async () => {
  const { opened, fetched } = rig({ body: new TextEncoder().encode('<html>secret</html>'), type: 'text/html' });
  (await handlers())[MSG.PAGE_OPEN]({ url: 'https://cdn.example/a.png' }, sender);
  await settle();
  assert.deepEqual(fetched, ['https://cdn.example/a.png']);
  assert.deepEqual(opened, []);
});

test('an image under a generic content type still opens, labelled by its signature', async () => {
  const { opened } = rig({ type: 'application/octet-stream' });
  (await handlers())[MSG.PAGE_OPEN]({ url: 'https://cdn.example/a.png', newTab: true }, sender);
  await settle();
  assert.equal(opened.length, 1);
  assert.match(decodeURIComponent(opened[0].url), /data:image\/png;base64/);
});

test('the guard judges the sender\'s tab, so a forged resource unlocks no private host', async () => {
  const { opened, fetched } = rig();
  (await handlers())[MSG.PAGE_OPEN]({ url: 'http://192.168.1.10/admin.png', resource: 'http://192.168.1.10/' }, sender);
  await settle();
  assert.deepEqual(fetched, [], 'refused before any request');
  assert.deepEqual(opened, []);
});

test('a hand-off records the sender\'s page as its resource, never the one the page names', async () => {
  const { opened } = rig();
  (await handlers())[MSG.PAGE_OPEN]({ url: 'https://cdn.example/p.png', resource: 'https://forged.example/', newTab: true }, sender);
  await settle();
  const url = String(opened[0]?.url || '');
  const payload = JSON.parse(decodeURIComponent(url.slice(url.indexOf('#stencil=') + 9)));
  assert.equal(payload.resource, PAGE);
});

test('a pin records the sender\'s page, never the resource the page names', async () => {
  rig();
  (await handlers())[MSG.PAGE_PIN]({ url: 'https://cdn.example/a.png', pin: true, resource: 'https://bank.example/' }, sender);
  await settle();
  const pins = (await chrome.storage.local.get(PINS_KEY))[PINS_KEY] || [];
  assert.equal(pins.length, 1);
  assert.equal(pins[0].resource, PAGE);
  assert.equal(pins[0].site, 'https://site.example');
});

test('OPEN_TAB opens the editor or this extension\'s own page, and no other scheme', async () => {
  const { opened } = rig();
  const h = await handlers();
  for (const url of ['https://editor.example/#stencil=x', 'chrome-extension://ext/src/crop/crop.html?k=1',
    'javascript:alert(1)', 'file:///etc/passwd', 'chrome://settings', 'chrome-extension://other/page.html']) {
    h[MSG.OPEN_TAB]({ url });
  }
  await settle();
  assert.deepEqual(opened.map((o) => o.url), ['https://editor.example/#stencil=x', 'chrome-extension://ext/src/crop/crop.html?k=1']);
});
