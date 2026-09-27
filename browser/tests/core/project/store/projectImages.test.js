// BR-7: a project's image lives under its own key, so a save re-serialises the small layout and
// writes the image only when it changed. A payload an older build wrote, image inline, still loads,
// and its next save moves the image out.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ProjectsStore, REGISTRY_KEY, PROJECT_PREFIX } from '../../../../js/core/project/store/projectsStore.js';
import { IMAGE_PREFIX } from '../../../../js/core/project/store/projectImages.js';
import { createMemoryStorage } from '../../../helpers/memoryStorage.js';

const PNG = (n) => `data:image/png;base64,${'iVBOR'.repeat(200)}${n}`;
const LAYOUT = { imageWidth: 4, imageHeight: 3, lines: [{ points: [{ x: 1, y: 2 }] }] };

const recording = (init) => {
  const storage = createMemoryStorage(init);
  const writes = [];
  const set = storage.setItem.bind(storage);
  storage.setItem = (k, v) => { writes.push([k, String(v)]); set(k, v); };
  return { storage, writes };
};

test('the image is stored under its own key, never inside the payload JSON', () => {
  const { storage } = recording();
  const s = new ProjectsStore(storage);
  s.upsert({ id: 'p', name: 'P' }, { image: PNG(1), layout: LAYOUT });
  assert.equal(storage.getItem(IMAGE_PREFIX + 'p'), PNG(1));
  assert.ok(!storage.getItem(PROJECT_PREFIX + 'p').includes('base64'), 'the payload is the layout alone');
  assert.deepEqual(s.get('p').payload, { image: PNG(1), layout: LAYOUT });
});

test('a save with the same image rewrites the layout and the registry, not the image', () => {
  const { storage, writes } = recording();
  const s = new ProjectsStore(storage);
  const image = PNG(2);
  s.upsert({ id: 'p', name: 'P' }, { image, layout: LAYOUT });
  writes.length = 0;
  for (let i = 0; i < 3; i++) s.upsert({ id: 'p', name: 'P' }, { image, layout: { ...LAYOUT, lines: [] } });
  assert.deepEqual(writes.map(([k]) => k), Array(3).fill([PROJECT_PREFIX + 'p', REGISTRY_KEY]).flat());
  assert.ok(writes.every(([, v]) => !v.includes('iVBOR')), 'no write carried the image');
  s.upsert({ id: 'p', name: 'P' }, { image: PNG(3), layout: LAYOUT });
  assert.equal(writes.filter(([k]) => k === IMAGE_PREFIX + 'p').length, 1, 'a new image is written once');
  assert.equal(s.get('p').payload.image, PNG(3));
});

test('a save with no image clears the key; the payload reads back with image null', () => {
  const s = new ProjectsStore(createMemoryStorage());
  s.upsert({ id: 'p', name: 'P' }, { image: PNG(4), layout: LAYOUT });
  s.upsert({ id: 'p', name: 'P' }, { image: null, layout: LAYOUT });
  assert.equal(s.get('p').payload.image, null);
});

test('a payload an older build wrote, image inline, loads as it was — and its next save moves it out', () => {
  const storage = createMemoryStorage({
    [REGISTRY_KEY]: JSON.stringify([{ id: 'old', name: 'Old', updatedAt: 1 }]),
    [PROJECT_PREFIX + 'old']: JSON.stringify({ image: PNG(5), layout: LAYOUT }),
  });
  const s = new ProjectsStore(storage);
  const proj = s.get('old');
  assert.deepEqual(proj.payload, { image: PNG(5), layout: LAYOUT });
  s.upsert(proj.meta, proj.payload);
  assert.equal(storage.getItem(IMAGE_PREFIX + 'old'), PNG(5));
  assert.ok(!storage.getItem(PROJECT_PREFIX + 'old').includes('base64'));
  assert.deepEqual(s.get('old').payload, { image: PNG(5), layout: LAYOUT });
});

test('an older build writing the image inline again wins over the key', () => {
  const storage = createMemoryStorage();
  const s = new ProjectsStore(storage);
  s.upsert({ id: 'p', name: 'P' }, { image: PNG(6), layout: LAYOUT });
  storage.setItem(PROJECT_PREFIX + 'p', JSON.stringify({ image: PNG(7), layout: LAYOUT }));
  assert.equal(s.get('p').payload.image, PNG(7));
});

test('a quota failure on the image leaves the payload and the registry as they were', () => {
  const storage = createMemoryStorage();
  const s = new ProjectsStore(storage);
  s.upsert({ id: 'p', name: 'P' }, { image: PNG(8), layout: LAYOUT });
  const before = [storage.getItem(PROJECT_PREFIX + 'p'), storage.getItem(REGISTRY_KEY)];
  storage.throwOnSet = true;
  assert.throws(() => s.upsert({ id: 'p', name: 'Q' }, { image: PNG(9), layout: {} }), { name: 'QuotaExceededError' });
  assert.deepEqual([storage.getItem(PROJECT_PREFIX + 'p'), storage.getItem(REGISTRY_KEY)], before);
});

test('remove and clearAll take the image keys with them', () => {
  const storage = createMemoryStorage();
  const s = new ProjectsStore(storage);
  s.upsert({ id: 'a', name: 'A' }, { image: PNG(10), layout: {} });
  s.upsert({ id: 'b', name: 'B' }, { image: PNG(11), layout: {} });
  s.remove('a');
  assert.equal(storage.getItem(IMAGE_PREFIX + 'a'), null);
  assert.equal(storage.getItem(IMAGE_PREFIX + 'b'), PNG(11));
  s.clearAll();
  assert.deepEqual([...storage._map.keys()], []);
});
