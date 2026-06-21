// Tests for the pure pinned-store helpers (src/lib/pins.js). The async wrappers
// (loadPins/setPinned) touch chrome.storage and are exercised in the browser; here we
// cover the pure list transforms that decide identity, dedupe, ordering, and grouping.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { siteOf, pinKey, isPinnedIn, matchPinsForSite, sitesOf, addPinEntry, removePinEntry, loadPins, setPinned, PINS_KEY } from '../src/lib/pins.js';

// Minimal chrome.storage.local mock with an awaitable get/set, so the async wrappers
// (loadPins/setPinned) can be driven from Node. The deferred resolution models real
// storage latency — the gap during which a concurrent read-modify-write could race.
const installStorageMock = () => {
  let store = {};
  globalThis.chrome = {
    storage: {
      local: {
        get: async (key) => (key in store ? { [key]: store[key] } : {}),
        set: async (obj) => { await Promise.resolve(); Object.assign(store, obj); },
      },
    },
  };
  return { reset: () => { store = {}; } };
};

const pin = (site, source, extra = {}) => ({ site, source, name: source, kind: 'image', t: 1, ...extra });

test('siteOf returns the origin, or "" when unparseable', () => {
  assert.equal(siteOf('https://example.com/a/b?x=1'), 'https://example.com');
  assert.equal(siteOf('http://host:8080/p'), 'http://host:8080');
  assert.equal(siteOf('not a url'), '');
  assert.equal(siteOf(''), '');
  assert.equal(siteOf(null), '');
});

test('pinKey + isPinnedIn key on (site, source) together', () => {
  const entries = [pin('https://a.com', 'https://cdn/x.png')];
  assert.equal(pinKey('https://a.com', 'https://cdn/x.png'), 'https://a.com\nhttps://cdn/x.png');
  assert.equal(isPinnedIn(entries, 'https://a.com', 'https://cdn/x.png'), true);
  // Same image, different site → not pinned there.
  assert.equal(isPinnedIn(entries, 'https://b.com', 'https://cdn/x.png'), false);
  // Same site, different image → not pinned.
  assert.equal(isPinnedIn(entries, 'https://a.com', 'https://cdn/y.png'), false);
  assert.equal(isPinnedIn([], 'https://a.com', 'https://cdn/x.png'), false);
});

test('addPinEntry prepends, dedupes on (site, source), and refreshes the timestamp', () => {
  let list = [];
  list = addPinEntry(list, pin('https://a.com', 'https://cdn/x.png', { t: 1 }));
  list = addPinEntry(list, pin('https://a.com', 'https://cdn/y.png', { t: 2 }));
  assert.deepEqual(list.map((e) => e.source), ['https://cdn/y.png', 'https://cdn/x.png']); // newest-first

  // Re-pin x → one entry, floated to the front, timestamp refreshed.
  list = addPinEntry(list, pin('https://a.com', 'https://cdn/x.png', { t: 9 }));
  assert.equal(list.length, 2);
  assert.deepEqual(list.map((e) => e.source), ['https://cdn/x.png', 'https://cdn/y.png']);
  assert.equal(list[0].t, 9);

  // Same source on a different site is a distinct pin.
  list = addPinEntry(list, pin('https://b.com', 'https://cdn/x.png', { t: 3 }));
  assert.equal(list.length, 3);
});

test('addPinEntry normalizes fields and defaults kind/timestamp', () => {
  const [e] = addPinEntry([], { source: '  https://cdn/x.png  ', site: ' https://a.com ', resource: '', name: ' x ' });
  assert.equal(e.source, 'https://cdn/x.png');
  assert.equal(e.site, 'https://a.com');
  assert.equal(e.name, 'x');
  assert.equal(e.kind, 'image');           // defaulted
  assert.equal(typeof e.t, 'number');      // stamped
});

test('addPinEntry caps the list at 500 (newest kept)', () => {
  let list = [];
  for (let i = 0; i < 520; i++) list = addPinEntry(list, pin('https://a.com', `https://cdn/${i}.png`, { t: i }));
  assert.equal(list.length, 500);
  assert.equal(list[0].source, 'https://cdn/519.png');     // newest at front
  assert.equal(list.at(-1).source, 'https://cdn/20.png');  // oldest 20 dropped
});

test('removePinEntry drops only the matching (site, source), returning the same ref when nothing matched', () => {
  const list = [pin('https://a.com', 'https://cdn/x.png'), pin('https://a.com', 'https://cdn/y.png')];
  const after = removePinEntry(list, 'https://a.com', 'https://cdn/x.png');
  assert.deepEqual(after.map((e) => e.source), ['https://cdn/y.png']);
  // No match → identical reference (lets callers skip a needless write).
  assert.equal(removePinEntry(list, 'https://a.com', 'https://cdn/none.png'), list);
});

test('matchPinsForSite + sitesOf group by site, preserving newest-first order', () => {
  const list = [
    pin('https://b.com', 'https://cdn/3.png'),
    pin('https://a.com', 'https://cdn/2.png'),
    pin('https://a.com', 'https://cdn/1.png'),
  ];
  assert.deepEqual(matchPinsForSite(list, 'https://a.com').map((e) => e.source), ['https://cdn/2.png', 'https://cdn/1.png']);
  assert.deepEqual(matchPinsForSite(list, 'https://b.com').map((e) => e.source), ['https://cdn/3.png']);
  assert.deepEqual(sitesOf(list), ['https://b.com', 'https://a.com']);   // distinct, first-seen order
  assert.deepEqual(sitesOf([]), []);
});

test('setPinned serializes concurrent pins so none clobber each other', async () => {
  installStorageMock();
  const site = 'https://en.wikipedia.org';
  // Fire 10 pins concurrently (the `stencil.pin([...])` batch shape). Without the write
  // queue every call reads the same empty `before` and the last set() wins → 1 survives.
  await Promise.all(
    Array.from({ length: 10 }, (_, i) =>
      setPinned({ source: `https://cdn/img${i}.png`, site, name: `img${i}`, kind: 'image', pinned: true })),
  );
  const list = await loadPins();
  assert.equal(list.length, 10, 'all 10 concurrent pins persisted');
  assert.equal(new Set(list.map((e) => e.source)).size, 10, 'no pins lost or duplicated');
});

test('setPinned interleaves concurrent pin + unpin deterministically', async () => {
  const mock = installStorageMock();
  mock.reset();
  const site = 'https://a.com';
  // Pin three, then concurrently unpin one while pinning a fourth — the unpin must see
  // the earlier pins (serialized), not an empty stale snapshot.
  await Promise.all([
    setPinned({ source: 'https://cdn/1.png', site, pinned: true }),
    setPinned({ source: 'https://cdn/2.png', site, pinned: true }),
    setPinned({ source: 'https://cdn/3.png', site, pinned: true }),
  ]);
  await Promise.all([
    setPinned({ source: 'https://cdn/2.png', site, pinned: false }),
    setPinned({ source: 'https://cdn/4.png', site, pinned: true }),
  ]);
  const sources = (await loadPins()).map((e) => e.source).sort();
  assert.deepEqual(sources, ['https://cdn/1.png', 'https://cdn/3.png', 'https://cdn/4.png']);
});
