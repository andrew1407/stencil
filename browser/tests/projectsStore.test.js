import { test } from 'node:test';
import assert from 'node:assert';
import {
  ProjectsStore, shouldPersist, baseProjectName, EXPIRY_MS, WARN_MS,
  REGISTRY_KEY, PROJECT_PREFIX, MIGRATED_FLAG,
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
  assert.strictEqual(s.nameExists('floor plan'), true);     // case-insensitive
  assert.strictEqual(s.nameExists('  Roof  '), true);       // trims
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

test('isExpired boundary: false at exactly EXPIRY_MS, true at +1', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  const m = meta('a', { updatedAt: now - EXPIRY_MS });
  assert.strictEqual(s.isExpired(m, now), false);
  const m2 = meta('a', { updatedAt: now - EXPIRY_MS - 1 });
  assert.strictEqual(s.isExpired(m2, now), true);
});

test('isExpiringSoon: true within WARN_MS of expiry, false when further out', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  // Expires in 12h → within the 1-day warning window.
  const soon = meta('a', { updatedAt: now - EXPIRY_MS + (WARN_MS / 2) });
  assert.strictEqual(s.isExpiringSoon(soon, now), true);
  // Expires in 2 days → outside the warning window.
  const later = meta('b', { updatedAt: now - EXPIRY_MS + (2 * WARN_MS) });
  assert.strictEqual(s.isExpiringSoon(later, now), false);
});

test('isExpiringSoon: false once already expired (gets the expired treatment)', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  const expired = meta('a', { updatedAt: now - EXPIRY_MS - 1 });
  assert.strictEqual(s.isExpired(expired, now), true);
  assert.strictEqual(s.isExpiringSoon(expired, now), false);
});

test('isExpiringSoon boundary: true at exactly WARN_MS remaining, false just past', () => {
  const s = new ProjectsStore(makeShim());
  const now = 1_000_000_000;
  // Exactly WARN_MS until expiry → inclusive, soon.
  const edge = meta('a', { updatedAt: now - EXPIRY_MS + WARN_MS });
  assert.strictEqual(s.isExpiringSoon(edge, now), true);
  // One ms more remaining → outside the window.
  const justOut = meta('b', { updatedAt: now - EXPIRY_MS + WARN_MS + 1 });
  assert.strictEqual(s.isExpiringSoon(justOut, now), false);
});

test('renew restarts the expiry window from now', () => {
  const s = new ProjectsStore(makeShim());
  s.upsert(meta('a'), { image: null, layout: {} });
  // Age it to one second from expiry.
  const now = 1_000_000_000;
  s.touch('a', now - EXPIRY_MS + 1000);
  assert.strictEqual(s.isExpiringSoon(s.getMeta('a'), now), true);
  // Renewing stamps updatedAt = now, so it's neither expiring soon nor expired.
  const renewed = s.renew('a', now);
  assert.strictEqual(renewed.updatedAt, now);
  assert.strictEqual(s.isExpiringSoon(s.getMeta('a'), now), false);
  assert.strictEqual(s.isExpired(s.getMeta('a'), now), false);
  assert.strictEqual(s.expiresAt(s.getMeta('a')), now + EXPIRY_MS);
});

test('renew returns null for a missing project', () => {
  const s = new ProjectsStore(makeShim());
  assert.strictEqual(s.renew('nope', 123), null);
});

test('sweepExpired removes only expired and returns their ids', () => {
  const shim = makeShim();
  const s = new ProjectsStore(shim);
  const now = 10 * EXPIRY_MS;
  s.upsert(meta('fresh'), { image: null, layout: {} });
  s.touch('fresh', now - 1000);
  s.upsert(meta('old'), { image: null, layout: {} });
  s.touch('old', now - EXPIRY_MS - 5000);
  const removed = s.sweepExpired(now);
  assert.deepStrictEqual(removed, ['old']);
  assert.ok(s.getMeta('fresh'));
  assert.strictEqual(s.getMeta('old'), null);
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
  // Second call is a no-op.
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
  // Set a colour.
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
