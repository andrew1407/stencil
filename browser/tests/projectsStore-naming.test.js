// js/core/projectsStore.js naming and colour: shouldPersist, the copy-suffix rules, rename,
// the project/blank colours and findByImage. Split from projectsStore.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { ProjectsStore, shouldPersist, baseProjectName } from '../js/core/project/store/projectsStore.js';
import { createMemoryStorage } from './helpers/memoryStorage.js';

// Map-backed localStorage shim (shared helper): exposes keys() for the store's
// enumeration, and `throwOnSet` for the QuotaExceededError path.
const makeShim = (opts = {}) => createMemoryStorage({}, opts);

const meta = (id, over = {}) => ({
  id, name: over.name ?? id, thumbnail: null,
  createdAt: over.createdAt ?? 1000, updatedAt: over.updatedAt ?? 1000,
  hasImage: false, imageW: null, imageH: null, ...over,
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
