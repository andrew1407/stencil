// Quick crop's hand-off (lib/stencil.js launchCrop → takeCropHandoff): one session entry per
// launch keyed by the nonce in the page URL, removed once read, and a refused set surfaced.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from '../helpers/chromeStub.js';
import { launchCrop, takeCropHandoff, CROP_KEY_PREFIX } from '../../src/lib/stencil.js';

const EXT = 'chrome-extension://abc/';
const install = (opts) => {
  const stub = installChromeStub(opts);
  const opened = [];
  globalThis.chrome.runtime.getURL = (p) => EXT + p;
  globalThis.chrome.tabs = { create: async ({ url }) => { opened.push(url); return { url }; } };
  return { stub, opened };
};
const searchOf = (url) => new URL(url).search;

test('two launches get two keys, and each page reads its own image', async () => {
  const { stub, opened } = install();
  try {
    await launchCrop({ src: 'data:image/png;base64,ONE', source: 'https://a/1.png', resource: 'https://a/' });
    await launchCrop({ src: 'data:image/png;base64,TWO', source: 'https://a/2.png', resource: 'https://a/' });
    const keys = Object.keys(stub.peekSession());
    assert.equal(keys.length, 2);
    assert.ok(keys.every((k) => k.startsWith(CROP_KEY_PREFIX)));
    assert.ok(opened.every((u) => u.startsWith(`${EXT}src/crop/crop.html?k=`)));

    const second = await takeCropHandoff(searchOf(opened[1]));
    assert.deepEqual(second, { src: 'data:image/png;base64,TWO', source: 'https://a/2.png', resource: 'https://a/', error: '' });
    const first = await takeCropHandoff(searchOf(opened[0]));
    assert.equal(first.src, 'data:image/png;base64,ONE');
    assert.deepEqual(Object.keys(stub.peekSession()), [], 'each entry is removed once read');
    assert.equal((await takeCropHandoff(searchOf(opened[0]))).src, '', 'a reload does not reopen an old image');
  } finally { stub.restore(); }
});

test('a refused set opens the page with the reason, never the previous image', async () => {
  const { stub, opened } = install({ session: { [`${CROP_KEY_PREFIX}old`]: { src: 'data:old', t: Date.now() } } });
  globalThis.chrome.storage.session.set = async () => { throw new Error('QUOTA_BYTES quota exceeded'); };
  try {
    await launchCrop({ src: 'data:image/png;base64,BIG' });
    const handoff = await takeCropHandoff(searchOf(opened[0]));
    assert.equal(handoff.src, '');
    assert.match(handoff.error, /quota exceeded/);
  } finally { stub.restore(); }
});

test('?src= still wins, and an unread entry past its minute is swept by the next launch', async () => {
  const { stub, opened } = install({ session: { [`${CROP_KEY_PREFIX}stale`]: { src: 'data:x', t: Date.now() - 120_000 } } });
  try {
    assert.equal((await takeCropHandoff('?src=https%3A%2F%2Fa%2Fb.png')).src, 'https://a/b.png');
    await launchCrop({ src: 'data:image/png;base64,NEW' });
    assert.deepEqual(Object.keys(stub.peekSession()), [`${CROP_KEY_PREFIX}${new URLSearchParams(searchOf(opened[0])).get('k')}`]);
  } finally { stub.restore(); }
});

test('the sweep reads only the crop entries, never the whole session store', async () => {
  const other = { token: 'secret-ish', big: 'x'.repeat(1000) };
  const { stub } = install({ session: { ...other, [`${CROP_KEY_PREFIX}stale`]: { t: 0 }, [`${CROP_KEY_PREFIX}fresh`]: { t: Date.now() } } });
  try {
    await launchCrop({ src: 'data:image/png;base64,NEW' });
    assert.deepEqual(stub.sessionGets, [[`${CROP_KEY_PREFIX}stale`, `${CROP_KEY_PREFIX}fresh`]]);
    const left = Object.keys(stub.peekSession());
    assert.ok(!left.includes(`${CROP_KEY_PREFIX}stale`) && left.includes(`${CROP_KEY_PREFIX}fresh`));
    assert.ok(left.includes('token') && left.includes('big'), 'other values are left alone');
  } finally { stub.restore(); }
});

test('without getKeys (Chrome before 130) the sweep falls back to one whole-store read', async () => {
  const { stub } = install({ session: { [`${CROP_KEY_PREFIX}stale`]: { t: 0 } } });
  delete globalThis.chrome.storage.session.getKeys;
  try {
    await launchCrop({ src: 'data:image/png;base64,NEW' });
    assert.deepEqual(stub.sessionGets, [null]);
    assert.ok(!(`${CROP_KEY_PREFIX}stale` in stub.peekSession()));
  } finally { stub.restore(); }
});
