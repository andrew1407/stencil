import { test } from 'node:test';
import assert from 'node:assert';
import {
  ProjectsStore, shouldPersist, EXPIRY_MS,
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
