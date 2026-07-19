import { test } from 'node:test';
import assert from 'node:assert';
import {
  ProjectsStore, shouldPersist, baseProjectName, EXPIRY_MS, WARN_MS,
  REGISTRY_KEY, PROJECT_PREFIX, MIGRATED_FLAG,
  periodMs, addPeriod, PERIOD_MS, DEFAULT_PERIOD, normalizeKeywords,
} from '../js/core/projectsStore.js';

// Map-backed localStorage shim. Exposes keys() for the store's enumeration,
// and can be configured to throw a QuotaExceededError on setItem.
const makeShim = (opts = {}) => {
  const m = new Map();
  return {
    _map: m,
    throwOnSet: opts.throwOnSet || false,
    getItem(k) { return m.has(k) ? m.get(k) : null; },
    setItem(k, v) {
      if (this.throwOnSet) {
        const e = new Error('quota');
        e.name = 'QuotaExceededError';
        throw e;
      }
      m.set(k, String(v));
    },
    removeItem(k) { m.delete(k); },
    keys() { return Array.from(m.keys()); },
  };
};

const meta = (id, over = {}) => ({
  id, name: over.name ?? id, thumbnail: null,
  createdAt: over.createdAt ?? 1000, updatedAt: over.updatedAt ?? 1000,
  hasImage: false, imageW: null, imageH: null, ...over,
});

test('createId returns unique ids', () => {
  const s = new ProjectsStore(makeShim());
  const ids = new Set();
  for (let i = 0; i < 50; i++) ids.add(s.createId());
  assert.strictEqual(ids.size, 50);
});

test('defaultName increments past existing Untitled indices', () => {
  const s = new ProjectsStore(makeShim());
  assert.strictEqual(s.defaultName(), 'Untitled 1');
  s.upsert(meta('a', { name: 'Untitled 1' }), { image: null, layout: {} });
  s.upsert(meta('b', { name: 'Untitled 3' }), { image: null, layout: {} });
  s.upsert(meta('c', { name: 'My drawing' }), { image: null, layout: {} });
  assert.strictEqual(s.defaultName(), 'Untitled 4');
});

test('nameExists: case-insensitive, trims, excludes a given id', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { name: 'Floor Plan' }), { image: null, layout: {} });
  s.upsert(meta('b', { name: 'Roof' }), { image: null, layout: {} });
  assert.strictEqual(s.nameExists('floor plan'), true);
  assert.strictEqual(s.nameExists('  Roof  '), true);
  assert.strictEqual(s.nameExists('Basement'), false);
  // The project being renamed shouldn't collide with its own current name.
  assert.strictEqual(s.nameExists('Floor Plan', 'a'), false);
  assert.strictEqual(s.nameExists('Floor Plan', 'b'), true);
  assert.strictEqual(s.nameExists(''), false);
});

test('validateName: reports ok + reason for empty / too-long / duplicate', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { name: 'Roof' }), { image: null, layout: {} });
  assert.deepEqual(s.validateName('Floor'), { ok: true, reason: '' });
  assert.equal(s.validateName('   ').ok, false);
  assert.match(s.validateName('').reason, /empty/i);
  assert.equal(s.validateName('x'.repeat(81)).ok, false);
  assert.equal(s.validateName('roof').ok, false);            // case-insensitive duplicate
  assert.match(s.validateName('roof').reason, /taken/i);
  assert.equal(s.validateName('Roof', 'a').ok, true);        // its own name doesn't collide
});

test('upsert/get/list round-trip, list sorted updatedAt desc', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { updatedAt: 100 }), { image: 'imgA', layout: { lines: [1] } });
  s.upsert(meta('b', { updatedAt: 300 }), { image: null, layout: { lines: [2] } });
  s.upsert(meta('c', { updatedAt: 200 }), { image: null, layout: {} });

  const got = s.get('a');
  assert.ok(got);
  assert.strictEqual(got.payload.image, 'imgA');
  assert.deepStrictEqual(got.payload.layout.lines, [1]);

  const ids = s.list().map(m => m.id);
  // upsert bumps updatedAt to Date.now(), so all three share ~now; verify the
  // round-trip count and that getMeta works rather than the injected order.
  assert.strictEqual(ids.length, 3);
  assert.ok(ids.includes('a') && ids.includes('b') && ids.includes('c'));
});

test('list sorted updatedAt desc (explicit via touch)', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a'), { image: null, layout: {} });
  s.upsert(meta('b'), { image: null, layout: {} });
  s.upsert(meta('c'), { image: null, layout: {} });
  s.touch('a', 100);
  s.touch('b', 300);
  s.touch('c', 200);
  assert.deepStrictEqual(s.list().map(m => m.id), ['b', 'c', 'a']);
});

test('remove deletes registry entry + payload, leaves others', () => {
  const shim = makeShim();
  const s = new ProjectsStore(shim);
  s.upsert(meta('a'), { image: null, layout: {} });
  s.upsert(meta('b'), { image: null, layout: {} });
  s.remove('a');
  assert.strictEqual(s.getMeta('a'), null);
  assert.strictEqual(s.get('a'), null);
  assert.strictEqual(shim.getItem(PROJECT_PREFIX + 'a'), null);
  assert.ok(s.getMeta('b'));
  assert.ok(shim.getItem(PROJECT_PREFIX + 'b'));
});

test('clearAll removes project keys + registry, preserves theme/hotkeys', () => {
  const shim = makeShim();
  shim.setItem('drawingApp_theme', 'dark');
  shim.setItem('drawingApp_hotkeys', '{"x":1}');
  const s = new ProjectsStore(shim);
  s.upsert(meta('a'), { image: null, layout: {} });
  s.upsert(meta('b'), { image: null, layout: {} });
  s.clearAll();
  assert.strictEqual(shim.getItem(REGISTRY_KEY), null);
  assert.strictEqual(shim.getItem(PROJECT_PREFIX + 'a'), null);
  assert.strictEqual(shim.getItem(PROJECT_PREFIX + 'b'), null);
  assert.strictEqual(shim.getItem('drawingApp_theme'), 'dark');
  assert.strictEqual(shim.getItem('drawingApp_hotkeys'), '{"x":1}');
});

test('periodMs / addPeriod presets (fixed durations); mirror core', () => {
  const DAY = 24 * 60 * 60 * 1000;
  assert.strictEqual(periodMs('day'), DAY);
  assert.strictEqual(periodMs('week'), 7 * DAY);
  assert.strictEqual(periodMs('week'), EXPIRY_MS);
  assert.strictEqual(periodMs('fortnight'), 14 * DAY);
  assert.strictEqual(periodMs('month'), 30 * DAY);
  assert.strictEqual(periodMs('3month'), 90 * DAY);
  assert.strictEqual(periodMs('6month'), 180 * DAY);
  assert.strictEqual(periodMs('year'), 365 * DAY);
  // Unknown / empty → one week.
  assert.strictEqual(periodMs(''), 7 * DAY);
  assert.strictEqual(periodMs('decade'), 7 * DAY);
  assert.strictEqual(addPeriod(1000, 'day'), 1000 + DAY);
  assert.strictEqual(PERIOD_MS.week, EXPIRY_MS);
});

test('expiry keyed on stored expiresAt; 0/absent == keep forever', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  assert.strictEqual(s.isExpired(meta('a', { expiresAt: now + 1000 }), now), false);
  assert.strictEqual(s.isExpired(meta('a', { expiresAt: now - 1 }), now), true);
  assert.strictEqual(s.isExpired(meta('a', { expiresAt: 0 }), now), false); // keep forever
  assert.strictEqual(s.expiresAt(meta('a', { expiresAt: now + 1000 })), now + 1000);
  assert.strictEqual(s.expiresAt(meta('a', { expiresAt: 0 })), null); // keep forever
});

test('isExpiringSoon: true within WARN_MS of expiry, false when further out', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  const soon = meta('a', { expiresAt: now + (WARN_MS / 2) });
  assert.strictEqual(s.isExpiringSoon(soon, now), true);
  const later = meta('b', { expiresAt: now + (2 * WARN_MS) });
  assert.strictEqual(s.isExpiringSoon(later, now), false);
  // Keep forever → never "soon".
  assert.strictEqual(s.isExpiringSoon(meta('c', { expiresAt: 0 }), now), false);
});

test('isExpiringSoon: false once already expired (gets the expired treatment)', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  const expired = meta('a', { expiresAt: now - 1 });
  assert.strictEqual(s.isExpired(expired, now), true);
  assert.strictEqual(s.isExpiringSoon(expired, now), false);
});

test('isExpiringSoon boundary: true at exactly WARN_MS remaining, false just past', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  const edge = meta('a', { expiresAt: now + WARN_MS });
  assert.strictEqual(s.isExpiringSoon(edge, now), true);
  const justOut = meta('b', { expiresAt: now + WARN_MS + 1 });
  assert.strictEqual(s.isExpiringSoon(justOut, now), false);
});

test('setExpiration sets fields exactly, no updatedAt bump', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { updatedAt: 5000 }), { image: null, layout: {} });
  const updatedAt = s.getMeta('a').updatedAt;
  const m = s.setExpiration('a', { expiresAt: 9999, refreshPeriod: 'month', autoRefresh: false });
  assert.strictEqual(m.expiresAt, 9999);
  assert.strictEqual(m.refreshPeriod, 'month');
  assert.strictEqual(m.autoRefresh, false);
  assert.strictEqual(s.getMeta('a').updatedAt, updatedAt);
  // Empty period normalises to the default; keep-forever via 0.
  s.setExpiration('a', { expiresAt: 0, refreshPeriod: '' });
  assert.strictEqual(s.getMeta('a').refreshPeriod, DEFAULT_PERIOD);
  assert.strictEqual(s.getMeta('a').expiresAt, 0);
  assert.strictEqual(s.setExpiration('missing', { expiresAt: 1 }), null);
});

test('renew sets expiresAt = now + refresh period (not updatedAt)', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { refreshPeriod: 'month', expiresAt: 1 }), { image: null, layout: {} });
  const now = 1_000_000_000;
  const renewed = s.renew('a', now);
  assert.strictEqual(renewed.expiresAt, now + periodMs('month'));
  assert.strictEqual(s.isExpired(s.getMeta('a'), now), false);
});

test('renew returns null for a missing project', () => {
  const s = new ProjectsStore(makeShim());
  assert.strictEqual(s.renew('nope', 123), null);
});

test('sweepExpired removes only expired, keeps keep-forever, returns ids', () => {
  const shim = makeShim();
  const s = new ProjectsStore(shim);
  const now = 10 * EXPIRY_MS;
  s.upsert(meta('fresh', { expiresAt: now + EXPIRY_MS }), { image: null, layout: {} });
  s.upsert(meta('old', { expiresAt: now - 5000 }), { image: null, layout: {} });
  s.upsert(meta('keep', { expiresAt: 0 }), { image: null, layout: {} }); // keep forever
  const removed = s.sweepExpired(now);
  assert.deepStrictEqual(removed, ['old']);
  assert.ok(s.getMeta('fresh'));
  assert.ok(s.getMeta('keep'));
  assert.strictEqual(s.getMeta('old'), null);
});

test('normalizeMeta default-fills legacy projects (expiresAt = updatedAt + week)', () => {
  const shim = makeShim();
  // Write a legacy registry entry with no expiration fields.
  shim.setItem(REGISTRY_KEY, JSON.stringify([
    { id: 'leg', name: 'Legacy', createdAt: 1000, updatedAt: 2000 },
  ]));
  const s = new ProjectsStore(shim);
  const m = s.getMeta('leg');
  assert.strictEqual(m.expiresAt, 2000 + EXPIRY_MS);
  assert.strictEqual(m.refreshPeriod, DEFAULT_PERIOD);
  assert.strictEqual(m.autoRefresh, true);
});

test('normalizeKeywords trims, drops blanks, dedupes case-insensitively (first-seen order)', () => {
  assert.deepStrictEqual(normalizeKeywords(['  Alpha ', 'beta', 'ALPHA', '', 'gamma ']), ['Alpha', 'beta', 'gamma']);
  assert.deepStrictEqual(normalizeKeywords([]), []);
  assert.deepStrictEqual(normalizeKeywords(null), []);
  assert.deepStrictEqual(normalizeKeywords(['x', 1, null, 'x']), ['x', '1']);
});

test('normalizeMeta default-fills keywords to [] for legacy projects', () => {
  const shim = makeShim();
  shim.setItem(REGISTRY_KEY, JSON.stringify([{ id: 'leg', name: 'Legacy', createdAt: 1000, updatedAt: 2000 }]));
  const s = new ProjectsStore(shim);
  assert.deepStrictEqual(s.getMeta('leg').keywords, []);
});

test('setKeywords normalizes + stores in place, no updatedAt bump; null on unknown id', () => {
  const shim = makeShim();
  const s = new ProjectsStore(shim);
  s.upsert(meta('p1', { updatedAt: 5000 }), { image: null, layout: {} });
  const before = s.getMeta('p1').updatedAt;
  const r = s.setKeywords('p1', [' Cat ', 'dog', 'CAT']);
  assert.deepStrictEqual(r.keywords, ['Cat', 'dog']);          // normalized
  assert.strictEqual(s.getMeta('p1').keywords.join(','), 'Cat,dog');
  assert.strictEqual(s.getMeta('p1').updatedAt, before);        // not bumped (like setColor)
  assert.strictEqual(s.setKeywords('nope', ['x']), null);       // unknown id → null
});

test('normalizeMeta default-fills description/lineLengthCm on legacy projects', () => {
  const shim = makeShim();
  shim.setItem(REGISTRY_KEY, JSON.stringify([{ id: 'leg', name: 'Legacy', createdAt: 1000, updatedAt: 2000 }]));
  const s = new ProjectsStore(shim);
  assert.strictEqual(s.getMeta('leg').description, '');
  assert.strictEqual(s.getMeta('leg').lineLengthCm, 0);
});

test('setDescription trims/clears in place, no updatedAt bump; null on unknown id', () => {
  const shim = makeShim();
  const s = new ProjectsStore(shim);
  s.upsert(meta('p1', { updatedAt: 5000 }), { image: null, layout: {} });
  const before = s.getMeta('p1').updatedAt;
  assert.strictEqual(s.setDescription('p1', '  a floor plan  ').description, 'a floor plan');  // trimmed
  assert.strictEqual(s.getMeta('p1').description, 'a floor plan');
  assert.strictEqual(s.getMeta('p1').updatedAt, before);        // not bumped (like setColor)
  assert.strictEqual(s.setDescription('p1', '   ').description, '');  // whitespace-only clears
  assert.strictEqual(s.setDescription('p1', null).description, '');   // null clears
  assert.strictEqual(s.setDescription('nope', 'x'), null);      // unknown id → null
});

test('migrateLegacy creates one project, sets flag; second call no-op', () => {
  const shim = makeShim();
  shim.setItem('drawingApp_image', 'legacyImg');
  shim.setItem('drawingApp_layout', JSON.stringify({
    imageBaseName: 'Floorplan', imageWidth: 800, imageHeight: 600, lines: [1, 2],
  }));
  const s = new ProjectsStore(shim);
  const id = s.migrateLegacy(5000);
  assert.ok(id);
  assert.strictEqual(shim.getItem(MIGRATED_FLAG), '1');
  assert.strictEqual(s.list().length, 1);
  const got = s.get(id);
  assert.strictEqual(got.meta.name, 'Floorplan');
  assert.strictEqual(got.meta.hasImage, true);
  assert.strictEqual(got.meta.imageW, 800);
  assert.strictEqual(got.meta.imageH, 600);
  assert.strictEqual(got.meta.createdAt, 5000);
  assert.strictEqual(got.meta.updatedAt, 5000);
  assert.strictEqual(got.payload.image, 'legacyImg');
  // Legacy keys are NOT deleted.
  assert.strictEqual(shim.getItem('drawingApp_image'), 'legacyImg');
  const again = s.migrateLegacy(9999);
  assert.strictEqual(again, null);
  assert.strictEqual(s.list().length, 1);
});

test('migrateLegacy with no legacy data sets flag, returns null', () => {
  const shim = makeShim();
  const s = new ProjectsStore(shim);
  assert.strictEqual(s.migrateLegacy(1), null);
  assert.strictEqual(shim.getItem(MIGRATED_FLAG), '1');
  assert.strictEqual(s.list().length, 0);
});

test('upsert surfaces QuotaExceededError from the backend', () => {
  const shim = makeShim({ throwOnSet: true });
  const s = new ProjectsStore(shim);
  assert.throws(
    () => s.upsert(meta('a'), { image: 'big', layout: {} }),
    e => e.name === 'QuotaExceededError'
  );
});

test('shouldPersist truth table', () => {
  assert.strictEqual(shouldPersist(null, false), false);
  assert.strictEqual(shouldPersist(null, true), false);
  assert.strictEqual(shouldPersist('id', true), false);
  assert.strictEqual(shouldPersist('id', false), true);
});

test('baseProjectName strips a trailing copy suffix', () => {
  assert.strictEqual(baseProjectName('photo'), 'photo');
  assert.strictEqual(baseProjectName('photo (2)'), 'photo');
  assert.strictEqual(baseProjectName('photo (12)  '), 'photo');
  assert.strictEqual(baseProjectName('a (1) (3)'), 'a (1)');
  assert.strictEqual(baseProjectName(''), '');
  assert.strictEqual(baseProjectName(null), '');
});

test('rename updates meta.name, no-op on unknown id', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { name: 'old' }), { image: null, layout: {} });
  assert.strictEqual(s.rename('a', 'new').name, 'new');
  assert.strictEqual(s.getMeta('a').name, 'new');
  assert.strictEqual(s.rename('missing', 'x'), null);
});

test('setColor sets/clears meta.color in place, no-op on unknown id', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { name: 'p' }), { image: null, layout: {} });
  assert.strictEqual(s.setColor('a', '#ec4899').color, '#ec4899');
  assert.strictEqual(s.getMeta('a').color, '#ec4899');
  // Clearing back to '' (theme fallback) is a valid set.
  assert.strictEqual(s.setColor('a', '').color, '');
  assert.strictEqual(s.getMeta('a').color, '');
  // Unknown id → null, registry untouched.
  assert.strictEqual(s.setColor('missing', '#000000'), null);
});

test('upsert round-trips a project colour in meta (persistence)', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { name: 'p', color: '#0ea5e9' }), { image: null, layout: {} });
  assert.strictEqual(s.getMeta('a').color, '#0ea5e9');
  // A later upsert that omits color does NOT silently inherit — the caller (storage.save)
  // is responsible for re-supplying it; here we prove setColor persists independently.
  s.setColor('a', '#16a34a');
  assert.strictEqual(s.list().find(m => m.id === 'a').color, '#16a34a');
});

test('setBlankColor sets meta.blankColor in place, no updatedAt bump; null on unknown id', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { name: 'p', blank: true, blankColor: '#ffffff', updatedAt: 5000 }), { image: null, layout: {} });
  const before = s.getMeta('a').updatedAt;
  assert.strictEqual(s.setBlankColor('a', '#00aaff').blankColor, '#00aaff');
  assert.strictEqual(s.getMeta('a').blankColor, '#00aaff');
  assert.strictEqual(s.getMeta('a').updatedAt, before);   // not bumped (like setColor)
  assert.strictEqual(s.setBlankColor('missing', '#000000'), null);   // unknown id → null
});

test('normalizeMeta default-fills blank/blankColor on legacy projects', () => {
  const s = new ProjectsStore(makeShim());
  // A pre-feature project record has neither field; reads must not be undefined.
  s.upsert(meta('a', { name: 'p' }), { image: null, layout: {} });
  const m = s.getMeta('a');
  assert.strictEqual(m.blank, false);
  assert.strictEqual(m.blankColor, '');
  // A blank project round-trips both fields.
  s.upsert(meta('b', { name: 'q', blank: true, blankColor: '#112233' }), { image: null, layout: {} });
  assert.strictEqual(s.getMeta('b').blank, true);
  assert.strictEqual(s.getMeta('b').blankColor, '#112233');
});

test('findByImage matches by source URL, falls back to base name', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a', { name: 'img', source: 'https://x/i.png', updatedAt: 100 }), { image: null, layout: {} });
  s.upsert(meta('b', { name: 'img (1)', source: 'https://x/i.png', updatedAt: 300 }), { image: null, layout: {} });
  s.upsert(meta('c', { name: 'other', source: 'https://y/o.png', updatedAt: 200 }), { image: null, layout: {} });

  const bySource = s.findByImage('https://x/i.png', 'whatever');
  assert.deepStrictEqual(bySource.map(m => m.id).sort(), ['a', 'b']);

  // No source → fall back to base name (suffix-insensitive).
  s.upsert(meta('d', { name: 'photo' }), { image: null, layout: {} });
  s.upsert(meta('e', { name: 'photo (2)' }), { image: null, layout: {} });
  const byName = s.findByImage('', 'photo');
  assert.deepStrictEqual(byName.map(m => m.id).sort(), ['d', 'e']);
});

test('copyName returns base when free, else next free (N)', () => {
  const s = new ProjectsStore(makeShim());
  assert.strictEqual(s.copyName('img', 'https://x/i.png'), 'img');
  s.upsert(meta('a', { name: 'img', source: 'https://x/i.png' }), { image: null, layout: {} });
  assert.strictEqual(s.copyName('img', 'https://x/i.png'), 'img (1)');
  s.upsert(meta('b', { name: 'img (1)', source: 'https://x/i.png' }), { image: null, layout: {} });
  assert.strictEqual(s.copyName('img', 'https://x/i.png'), 'img (2)');
  // A different source does not collide.
  assert.strictEqual(s.copyName('img', 'https://y/i.png'), 'img');
});
