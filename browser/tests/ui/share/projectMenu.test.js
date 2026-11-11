// ui/share/: the Copy › tree a project offers and how its entries hand things on — the clipboard
// write starts inside the click and waits for the load, a link click copies and a double click
// shares, and building the tree loads nothing.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { copyMenuItems, linkItems, shareMenuItems } from '../../../js/ui/share/projectMenu.js';
import { copyText, shareLink, shareImage } from '../../../js/ui/share/deliver.js';

const notices = [];
const writes = [];
globalThis.document ??= {};
globalThis.document.getElementById = (id) => (id === 'notify-balloon' ? { notify: (m, t) => notices.push([m, t]) } : null);
globalThis.location = { href: 'https://app.example/stencil/?project=Old' };
globalThis.ClipboardItem = class { constructor(parts) { this.parts = parts; } };
const shares = [];
const setNav = (extra = {}) => Object.defineProperty(globalThis, 'navigator', { configurable: true,
  value: { clipboard: { write: async (items) => { for (const it of items) for (const [type, p] of Object.entries(it.parts)) writes.push([type, await (await p).text?.() ?? p]); } }, ...extra } });
setNav();
const flush = () => new Promise((r) => setImmediate(r));
const labels = (items) => items.map((it) => it.label);

const storedRow = { id: 'p1', name: 'Plan', source: 'https://cdn.example/a.png', address: 'https://srv.example', remoteId: 'r9' };
const appWith = (loads) => ({
  image: null, activeProjectId: 'other',
  connections: { has: () => true },
  storage: { store: { getMeta: () => null } },
  projectTransfer: { storage: { store: { get: (id) => { loads.push(id); return { meta: {}, payload: { layout: { lines: [] } } }; } } } },
});

test('a stored row offers Copy › (name, three links, layout, project) and Copy image › beside it — and loads nothing', () => {
  const loads = [];
  const [copy, image] = copyMenuItems(appWith(loads), { meta: storedRow });
  assert.equal(copy.label, 'Copy entries');
  assert.deepEqual(labels(copy.items), ['Project name', 'Project link', 'Image link (source URL)', 'Server project link',
    'Layout (JSON)', 'Project (.stencil)']);
  assert.equal(image.label, 'Copy image');
  assert.deepEqual(labels(image.items), ['Current', 'No lines', 'Original'], 'not open, each is drawn off-screen once picked');
  assert.deepEqual(loads, []);
  const [plain, plainImage] = copyMenuItems(appWith(loads), { meta: { id: 'p2', name: 'Bare', source: 'data:image/png;base64,AA' } });
  assert.deepEqual(labels(plain.items), ['Project name', 'Project link', 'Layout (JSON)', 'Project (.stencil)']);
  assert.deepEqual(labels(plainImage.items), ['Current', 'No lines', 'Original']);
  assert.deepEqual(labels(copyMenuItems(appWith(loads))), ['Copy entries'], 'the open editor without a picture has nothing to copy as one');
});

test('the share button\'s menu: Image › three variants, then each link flat; a click shares, a double click copies', async () => {
  const app = { image: {}, imageBaseName: 'cat', imageSource: 'https://cdn.example/cat.png',
    remoteLink: { address: 'https://srv.example', remoteId: 'r1' }, activeProjectId: 'p1',
    storage: { store: { getMeta: () => ({ name: 'Cat' }) } } };
  const items = shareMenuItems(app);
  assert.deepEqual(labels(items), ['Image', 'Project link', 'Image link (source URL)', 'Server project link']);
  assert.deepEqual(labels(items[0].items), ['Current', 'No lines', 'Original']);
  assert.ok(items.every((it) => it.items || (it.onClick && it.onDoubleClick)));
  writes.length = 0;
  setNav({ share: async (data) => { shares.push(data); } });
  await items[2].onClick();
  assert.deepEqual(shares.at(-1), { title: 'Cat', url: 'https://app.example/stencil/?source=https%3A%2F%2Fcdn.example%2Fcat.png' });
  setNav();
  items[3].onDoubleClick();
  await flush();
  assert.deepEqual(writes, [['text/plain', 'https://app.example/stencil/?project=https%3A%2F%2Fsrv.example%2Fprojects%2Fr1']]);
  assert.deepEqual(labels(shareMenuItems({ ...app, image: null, imageSource: '', remoteLink: null })), ['Project link']);
});

test('the layout entry starts its clipboard write at once and fills it when the read lands', async () => {
  const loads = [];
  writes.length = 0;
  copyMenuItems(appWith(loads), { meta: { id: 'p3', name: 'L' } })[0].items.find((it) => it.label === 'Layout (JSON)').onClick();
  await flush();
  assert.deepEqual(loads, ['p3']);
  assert.equal(writes[0][0], 'text/plain');
  assert.deepEqual(JSON.parse(writes[0][1]).lines, []);
});

test('a link item copies its Stencil link on a click and shares it on a double click', async () => {
  writes.length = 0;
  const [name] = linkItems({ name: 'My Plan' });
  name.onClick();
  await flush();
  assert.deepEqual(writes, [['text/plain', 'https://app.example/stencil/?project=My+Plan']]);
  setNav({ share: async (data) => { shares.push(data); } });
  await name.onDoubleClick();
  assert.deepEqual(shares.at(-1), { title: 'My Plan', url: 'https://app.example/stencil/?project=My+Plan' });
  setNav();
});

test('with no share sheet a link is copied, and a picture without file sharing is copied too', async () => {
  writes.length = 0;
  notices.length = 0;
  await shareLink('https://app.example/?source=x');
  await shareImage(Promise.resolve(new Blob(['png'], { type: 'image/png' })), 'a.png');
  assert.deepEqual(writes.map(([type]) => type), ['text/plain', 'image/png']);
  assert.ok(notices.every(([, type]) => type === 'ok'));
  await copyText('x').catch(() => {});
});
