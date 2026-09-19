// The async half of src/lib/pins.js: clearPins and setPinned serialising against each other over
// chrome.storage, and the by-site grouping the options page renders from.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { clearPins, loadPins, setPinned, matchPinsForSite, sitesOf } from '../src/lib/pins.js';

// The stub's deferred set models the real latency gap a concurrent read-modify-write races in.
import { installChromeStub } from './helpers/chromeStub.js';

const installStorageMock = () => {
  const stub = installChromeStub();
  return { reset: stub.reset };
};

const pin = (site, source, extra = {}) => ({ site, source, name: source, kind: 'image', t: 1, ...extra });

test('clearPins("all") wipes every pin; a site scope wipes only that site', async () => {
  const mock = installStorageMock();
  mock.reset();
  const a = 'https://a.example', b = 'https://b.example';
  await setPinned({ source: 'https://cdn/1.png', site: a, pinned: true });
  await setPinned({ source: 'https://cdn/2.png', site: b, pinned: true });
  await setPinned({ source: 'https://cdn/3.png', site: a, pinned: true });

  // Scoped clear: only a.example's pins go; b.example's remain.
  await clearPins(a);
  assert.deepEqual((await loadPins()).map((e) => e.source), ['https://cdn/2.png']);

  // 'all' (and empty) clears everything that's left.
  await clearPins('all');
  assert.deepEqual(await loadPins(), []);
});

test('clearPins serializes against a concurrent setPinned', async () => {
  const mock = installStorageMock();
  mock.reset();
  const site = 'https://a.example';
  await setPinned({ source: 'https://cdn/1.png', site, pinned: true });
  // Clear-all racing a new pin: the write queue orders them, so exactly the later pin
  // survives (clear runs first, then the pin) — never a lost-update to an empty snapshot.
  await Promise.all([
    clearPins('all'),
    setPinned({ source: 'https://cdn/2.png', site, pinned: true }),
  ]);
  assert.deepEqual((await loadPins()).map((e) => e.source), ['https://cdn/2.png']);
});

test('matchPinsForSite + sitesOf group by site, preserving newest-first order', () => {
  const list = [
    pin('https://b.example', 'https://cdn/3.png'),
    pin('https://a.example', 'https://cdn/2.png'),
    pin('https://a.example', 'https://cdn/1.png'),
  ];
  assert.deepEqual(matchPinsForSite(list, 'https://a.example').map((e) => e.source), ['https://cdn/2.png', 'https://cdn/1.png']);
  assert.deepEqual(matchPinsForSite(list, 'https://b.example').map((e) => e.source), ['https://cdn/3.png']);
  assert.deepEqual(sitesOf(list), ['https://b.example', 'https://a.example']);   // distinct, first-seen order
  assert.deepEqual(sitesOf([]), []);
});

test('setPinned serializes concurrent pins so none clobber each other', async () => {
  installStorageMock();
  const site = 'https://github.com';
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
  const site = 'https://a.example';
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
