// BR-7: thumbnails live in one localStorage key per project, so a save re-serialises the small
// registry and never every 480px JPEG. A registry an older build wrote, thumbnails inline, still
// renders them and moves them to their keys on first read, losing nothing.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ProjectsStore, REGISTRY_KEY, PROJECT_PREFIX } from '../../../../js/core/project/store/projectsStore.js';
import { THUMB_PREFIX } from '../../../../js/core/project/store/projectThumbs.js';
import { createMemoryStorage } from '../../../helpers/memoryStorage.js';

const JPEG = (n) => `data:image/jpeg;base64,${'Q'.repeat(40)}${n}`;
const OLD_ROWS = [
  { id: 'a', name: 'Alpha', source: 'https://x/a.png', thumbnail: JPEG(1), updatedAt: 3, expiresAt: 0 },
  { id: 'b', name: 'Beta', thumbnail: null, updatedAt: 2 },
  { id: 'c', name: 'Gamma', thumbnail: JPEG(3), updatedAt: 1, keywords: ['k'] },
];
const oldRegistry = () => createMemoryStorage({ [REGISTRY_KEY]: JSON.stringify(OLD_ROWS) });
const stored = (storage) => JSON.parse(storage.getItem(REGISTRY_KEY));

test('an old registry still renders its thumbnails, moved to their own keys on first read', () => {
  const storage = oldRegistry();
  const list = new ProjectsStore(storage).list();
  assert.deepEqual(list.map((m) => [m.id, m.thumbnail]), [['a', JPEG(1)], ['b', null], ['c', JPEG(3)]]);
  assert.equal(storage.getItem(THUMB_PREFIX + 'a'), JPEG(1));
  assert.equal(storage.getItem(THUMB_PREFIX + 'c'), JPEG(3));
  assert.equal(storage.getItem(THUMB_PREFIX + 'b'), null);
  assert.deepEqual(stored(storage), OLD_ROWS.map((m) => ({ ...m, thumbnail: null })), 'every other field as it was');
  assert.ok(!storage.getItem(REGISTRY_KEY).includes('base64'));
});

test('another tab reading the migrated storage sees the same thumbnails', () => {
  const storage = oldRegistry();
  new ProjectsStore(storage).list();
  const peer = new ProjectsStore(storage);
  assert.equal(peer.getMeta('a').thumbnail, JPEG(1));
  assert.equal(peer.getMeta('b').thumbnail, null);
  assert.deepEqual(peer.list().map((m) => m.name), ['Alpha', 'Beta', 'Gamma']);
});

test('a save rewrites the registry without the thumbnail, and its key only when it changed', () => {
  const storage = createMemoryStorage();
  const s = new ProjectsStore(storage);
  const writes = [];
  const set = storage.setItem.bind(storage);
  storage.setItem = (k, v) => { writes.push(k); set(k, v); };
  s.upsert({ id: 'p', name: 'P', thumbnail: JPEG(7) }, { image: null, layout: {} });
  s.upsert({ ...s.getMeta('p'), name: 'P2' }, { image: null, layout: {} });
  assert.deepEqual(writes, [PROJECT_PREFIX + 'p', THUMB_PREFIX + 'p', REGISTRY_KEY, PROJECT_PREFIX + 'p', REGISTRY_KEY]);
  assert.ok(!storage.getItem(REGISTRY_KEY).includes('base64'));
  assert.equal(s.getMeta('p').thumbnail, JPEG(7));
  assert.equal(s.getMeta('p').name, 'P2');
});

test('a landed thumbnail writes its key and leaves the registry string untouched', () => {
  const storage = createMemoryStorage();
  const s = new ProjectsStore(storage);
  s.upsert({ id: 'p', name: 'P', thumbnail: null }, { image: null, layout: {} });
  const before = storage.getItem(REGISTRY_KEY);
  assert.equal(s.setThumbnail('p', JPEG(8)).thumbnail, JPEG(8));
  assert.equal(storage.getItem(REGISTRY_KEY), before);
  assert.equal(s.getMeta('p').thumbnail, JPEG(8));
  assert.equal(s.setThumbnail('nope', JPEG(9)), null);
  assert.equal(storage.getItem(THUMB_PREFIX + 'nope'), null);
});

test('a row replaced without a thumbnail clears its key, as the inline field was replaced', () => {
  const s = new ProjectsStore(createMemoryStorage());
  s.upsert({ id: 'p', name: 'P', thumbnail: JPEG(1) }, { image: null, layout: {} });
  s.upsert({ id: 'p', name: 'P', thumbnail: null }, { image: null, layout: {} });
  assert.equal(s.getMeta('p').thumbnail, null);
});

test('remove and clearAll take the thumbnail keys with them', () => {
  const storage = oldRegistry();
  const s = new ProjectsStore(storage);
  s.remove('a');
  assert.equal(storage.getItem(THUMB_PREFIX + 'a'), null);
  assert.equal(storage.getItem(THUMB_PREFIX + 'c'), JPEG(3));
  s.clearAll();
  assert.deepEqual([...storage._map.keys()], []);
});

test('a thumbnail key that cannot be written keeps its row inline: nothing is lost', () => {
  const storage = oldRegistry();
  const raw = storage.getItem(REGISTRY_KEY);
  storage.throwOnSet = true;
  const s = new ProjectsStore(storage);
  assert.deepEqual(s.list().map((m) => m.thumbnail), [JPEG(1), null, JPEG(3)]);
  assert.equal(storage.getItem(REGISTRY_KEY), raw);
  storage.throwOnSet = false;
  storage.setItem(REGISTRY_KEY, `${raw} `);
  assert.deepEqual(s.list().map((m) => m.thumbnail), [JPEG(1), null, JPEG(3)]);
  assert.equal(storage.getItem(THUMB_PREFIX + 'a'), JPEG(1), 'the next parse moves it');
});

test('an older build writing a thumbnail inline again wins over the key', () => {
  const storage = oldRegistry();
  const s = new ProjectsStore(storage);
  s.list();
  storage.setItem(REGISTRY_KEY, JSON.stringify([{ ...OLD_ROWS[0], thumbnail: JPEG(5) }]));
  assert.equal(s.getMeta('a').thumbnail, JPEG(5));
  assert.equal(storage.getItem(THUMB_PREFIX + 'a'), JPEG(5));
});
