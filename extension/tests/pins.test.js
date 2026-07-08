// Tests for the pure pinned-store helpers (src/lib/pins.js). The async wrappers
// (loadPins/setPinned) touch chrome.storage and are exercised in the browser; here we
// cover the pure list transforms that decide identity, dedupe, ordering, and grouping.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { siteOf, pinKey, isPinnedIn, matchPinsForSite, sitesOf, addPinEntry, removePinEntry, removeSiteEntries, clearPins, loadPins, setPinned, setPinKeywords, projectNameColor, normalizeKeywords, pinKeywords, pinMatchesSearch, PIN_SEARCH_MODES, PINS_KEY } from '../src/lib/pins.js';

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

test('addPinEntry carries color only for kind "project"', () => {
  // A project pin keeps its custom accent colour…
  const [proj] = addPinEntry([], { source: 'srv/p1', site: 'http://s', name: 'Shot', kind: 'project', color: '#ff0066' });
  assert.equal(proj.kind, 'project');
  assert.equal(proj.color, '#ff0066');
  // …an unset project colour normalizes to "" (theme default), not undefined.
  const [bare] = addPinEntry([], { source: 'srv/p2', site: 'http://s', name: 'Bare', kind: 'project' });
  assert.equal(bare.color, '');
  // A plain image pin never carries a color field (the popup leaves it theme-coloured).
  const [img] = addPinEntry([], pin('http://a.com', 'https://cdn/x.png', { color: '#ff0066' }));
  assert.equal('color' in img, false);
});

test('normalizeKeywords trims, drops blanks, dedupes case-insensitively (first-seen order)', () => {
  assert.deepEqual(normalizeKeywords([' Cat ', 'dog', 'CAT', '', 'Bird ']), ['Cat', 'dog', 'Bird']);
  assert.deepEqual(normalizeKeywords(null), []);
  assert.deepEqual(normalizeKeywords(['x', 1, null, 'x']), ['x', '1']);
});

test('addPinEntry stores normalized keywords, only when non-empty', () => {
  const [withKw] = addPinEntry([], pin('http://a', 'src1', { keywords: [' A ', 'b', 'A'] }));
  assert.deepEqual(withKw.keywords, ['A', 'b']);
  const [noKw] = addPinEntry([], pin('http://a', 'src2'));
  assert.equal('keywords' in noKw, false);   // empty → field omitted (lean)
  assert.deepEqual(pinKeywords(noKw), []);    // accessor still yields []
});

test('addPinEntry preserves keywords on a re-pin that omits them, overwrites when provided', () => {
  let list = addPinEntry([], pin('http://a', 'src', { keywords: ['keep', 'me'] }));
  // Re-pin WITHOUT keywords (e.g. a drag-to-pin) must not wipe them.
  list = addPinEntry(list, pin('http://a', 'src'));
  assert.deepEqual(list[0].keywords, ['keep', 'me']);
  // Re-pin WITH keywords overwrites.
  list = addPinEntry(list, pin('http://a', 'src', { keywords: ['fresh'] }));
  assert.deepEqual(list[0].keywords, ['fresh']);
});

test('setPinKeywords replaces/clears keywords on an existing pin; no-op on unknown', async () => {
  const mock = installStorageMock();
  mock.reset();
  await setPinned({ source: 'srcX', site: 'http://a', name: 'X', pinned: true });
  await setPinKeywords('http://a', 'srcX', [' foo ', 'bar', 'FOO']);
  assert.deepEqual((await loadPins())[0].keywords, ['foo', 'bar']);
  await setPinKeywords('http://a', 'srcX', []);   // clear
  assert.equal('keywords' in (await loadPins())[0], false);
  const before = await loadPins();
  assert.equal(await setPinKeywords('http://a', 'nope', ['z']), before);  // unknown → same list
});

test('pinMatchesSearch honors the search mode over name + keywords', () => {
  const p = { name: 'Sunset shot', keywords: ['beach', 'golden'] };
  assert.deepEqual(PIN_SEARCH_MODES, ['common', 'names', 'keywords']);
  assert.equal(pinMatchesSearch(p, '', 'common'), true);       // empty → all
  assert.equal(pinMatchesSearch(p, 'sun', 'names'), true);
  assert.equal(pinMatchesSearch(p, 'beach', 'names'), false);  // names mode ignores keywords
  assert.equal(pinMatchesSearch(p, 'beach', 'keywords'), true);
  assert.equal(pinMatchesSearch(p, 'sun', 'keywords'), false); // keywords mode ignores name
  assert.equal(pinMatchesSearch(p, 'golden', 'common'), true);
  assert.equal(pinMatchesSearch(p, 'sun', 'common'), true);
  assert.equal(pinMatchesSearch(p, 'nope', 'common'), false);
});

test('projectNameColor returns the custom hex, or the neutral-grey fallback when empty', () => {
  assert.equal(projectNameColor('#12ab34', 'var(--project-name-fg)'), '#12ab34');
  assert.equal(projectNameColor('  #12ab34  ', 'var(--project-name-fg)'), '#12ab34');   // trimmed
  assert.equal(projectNameColor('', 'var(--project-name-fg)'), 'var(--project-name-fg)'); // empty → fallback
  assert.equal(projectNameColor(null, 'var(--project-name-fg)'), 'var(--project-name-fg)');
  assert.equal(projectNameColor(undefined, '#888'), '#888');
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

test('removeSiteEntries drops every pin for one site, returning the same ref when nothing matched', () => {
  const list = [
    pin('https://a.com', 'https://cdn/1.png'),
    pin('https://b.com', 'https://cdn/2.png'),
    pin('https://a.com', 'https://cdn/3.png'),
  ];
  const after = removeSiteEntries(list, 'https://a.com');
  assert.deepEqual(after.map((e) => e.source), ['https://cdn/2.png']);   // only b.com survives
  // No pin for that site → identical reference (callers skip a needless write).
  assert.equal(removeSiteEntries(list, 'https://none.com'), list);
});

test('clearPins("all") wipes every pin; a site scope wipes only that site', async () => {
  const mock = installStorageMock();
  mock.reset();
  const a = 'https://a.com', b = 'https://b.com';
  await setPinned({ source: 'https://cdn/1.png', site: a, pinned: true });
  await setPinned({ source: 'https://cdn/2.png', site: b, pinned: true });
  await setPinned({ source: 'https://cdn/3.png', site: a, pinned: true });

  // Scoped clear: only a.com's pins go; b.com's remain.
  await clearPins(a);
  assert.deepEqual((await loadPins()).map((e) => e.source), ['https://cdn/2.png']);

  // 'all' (and empty) clears everything that's left.
  await clearPins('all');
  assert.deepEqual(await loadPins(), []);
});

test('clearPins serializes against a concurrent setPinned', async () => {
  const mock = installStorageMock();
  mock.reset();
  const site = 'https://a.com';
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
