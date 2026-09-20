import { test } from 'node:test';
import assert from 'node:assert';
import { ProjectsStore, REGISTRY_KEY, PROJECT_PREFIX } from '../js/core/projectsStore.js';
import { createMemoryStorage } from './helpers/memoryStorage.js';

// Map-backed localStorage shim (shared helper): exposes keys() for the store's
// enumeration, and `throwOnSet` for the QuotaExceededError path.
const makeShim = (opts = {}) => createMemoryStorage({}, opts);

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
