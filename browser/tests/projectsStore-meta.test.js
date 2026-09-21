// js/core/projectsStore.js metadata: normalizeMeta's legacy default-fill, keywords,
// description, the one-shot legacy migration and the quota error. From projectsStore.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  ProjectsStore, EXPIRY_MS, REGISTRY_KEY, MIGRATED_FLAG, DEFAULT_PERIOD, normalizeKeywords,
} from '../js/core/project/store/projectsStore.js';
import { createMemoryStorage } from './helpers/memoryStorage.js';

// Map-backed localStorage shim (shared helper): exposes keys() for the store's
// enumeration, and `throwOnSet` for the QuotaExceededError path.
const makeShim = (opts = {}) => createMemoryStorage({}, opts);

const meta = (id, over = {}) => ({
  id, name: over.name ?? id, thumbnail: null,
  createdAt: over.createdAt ?? 1000, updatedAt: over.updatedAt ?? 1000,
  hasImage: false, imageW: null, imageH: null, ...over,
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
