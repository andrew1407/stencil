// BR-7 over the IndexedDB backend: a thumbnail is a Blob written only when its picture changed and
// read back as one object URL; the image rides its own key; and a browser holding an older build's
// projects — thumbnails inline and under localStorage keys, images inline in the payload — loads
// every one of them after the upgrade, moved out of localStorage as it goes.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createProjectsBackend } from '../../../../js/core/project/store/projectsBackend.js';
import { ProjectsStore, REGISTRY_KEY, PROJECT_PREFIX } from '../../../../js/core/project/store/projectsStore.js';
import { THUMB_PREFIX } from '../../../../js/core/project/store/projectThumbs.js';
import { IMAGE_PREFIX } from '../../../../js/core/project/store/projectImages.js';
import { blobOfDataUrl } from '../../../../js/core/project/store/thumbBlobs.js';
import { createMemoryStorage } from '../../../helpers/memoryStorage.js';

const makeKv = (init = {}) => {
  const m = new Map(Object.entries(init));
  const kv = {
    _map: m, sets: [], gets: 0,
    async get(k) { kv.gets++; return m.has(k) ? m.get(k) : undefined; },
    async set(k, v) { kv.sets.push(k); m.set(k, v); },
    async remove(k) { m.delete(k); },
    async entries() { return Array.from(m.entries()); },
  };
  return kv;
};

const JPEG = (n) => `data:image/jpeg;base64,${btoa(`jpeg-bytes-${n}`)}`;
const PNG = (n) => `data:image/png;base64,${btoa(`png-bytes-${n}`.repeat(50))}`;
const TK = THUMB_PREFIX + 'p';
const bytesOf = async (blob) => Buffer.from(await blob.arrayBuffer()).toString();

test('blobOfDataUrl: base64 and percent-encoded bodies, with their media type', async () => {
  const b64 = blobOfDataUrl(JPEG(1));
  assert.equal(b64.type, 'image/jpeg');
  assert.equal(await bytesOf(b64), 'jpeg-bytes-1');
  const plain = blobOfDataUrl('data:text/plain,a%20b');
  assert.deepEqual([plain.type, await bytesOf(plain)], ['text/plain', 'a b']);
});

test('a thumbnail lands in IndexedDB as a Blob and reads back as one object URL', async () => {
  const ls = createMemoryStorage();
  const kv = makeKv();
  const store = new ProjectsStore(await createProjectsBackend({ storage: ls, kv }));
  store.upsert({ id: 'p', name: 'P', thumbnail: null }, { image: null, layout: {} });
  store.setThumbnail('p', JPEG(1));
  const record = kv._map.get(TK);
  assert.ok(record.blob instanceof Blob, 'stored as a Blob');
  assert.equal(await bytesOf(record.blob), 'jpeg-bytes-1');
  assert.equal(ls._map.has(TK), false, 'never in localStorage');
  const url = store.getMeta('p').thumbnail;
  assert.match(url, /^blob:/);
  assert.equal(store.list()[0].thumbnail, url, 'the same URL on every read');
});

test('the same picture is never written twice; a new one replaces it and its URL', async () => {
  const kv = makeKv();
  const store = new ProjectsStore(await createProjectsBackend({ storage: createMemoryStorage(), kv }));
  store.upsert({ id: 'p', name: 'P', thumbnail: JPEG(1) }, { image: null, layout: {} });
  const first = store.getMeta('p').thumbnail;
  store.setThumbnail('p', JPEG(1));
  store.upsert({ ...store.getMeta('p'), name: 'P2' }, { image: null, layout: {} });
  assert.equal(kv.sets.filter((k) => k === TK).length, 1, 'a save carrying the read URL writes nothing');
  store.setThumbnail('p', JPEG(2));
  assert.equal(kv.sets.filter((k) => k === TK).length, 2);
  assert.notEqual(store.getMeta('p').thumbnail, first);
  store.upsert({ ...store.getMeta('p'), thumbnail: first }, { image: null, layout: {} });
  assert.equal(await bytesOf(kv._map.get(TK).blob), 'jpeg-bytes-2', 'a stale URL never overwrites the picture');
});

test('the image and the payload ride IndexedDB, the image written only when it changed', async () => {
  const ls = createMemoryStorage();
  const kv = makeKv();
  const backend = await createProjectsBackend({ storage: ls, kv });
  const store = new ProjectsStore(backend);
  const image = PNG(1);
  store.upsert({ id: 'p', name: 'P' }, { image, layout: {} });
  store.upsert({ id: 'p', name: 'P' }, { image: store.get('p').payload.image, layout: { lines: [] } });
  await backend.flush();
  assert.deepEqual(kv.sets.filter((k) => k === IMAGE_PREFIX + 'p'), [IMAGE_PREFIX + 'p']);
  assert.equal(await bytesOf(kv._map.get(IMAGE_PREFIX + 'p').blob), 'png-bytes-1'.repeat(50), 'stored as a Blob');
  assert.ok(!kv._map.get(PROJECT_PREFIX + 'p').includes('base64'));
  assert.deepEqual([...ls._map.keys()], [REGISTRY_KEY], 'localStorage holds the registry alone');
});

test('refresh(id) reads a peer\'s payload, image and thumbnail, one read per key however many ask', async () => {
  const kv = makeKv();
  const backend = await createProjectsBackend({ storage: createMemoryStorage(), kv });
  const store = new ProjectsStore(backend);
  store.upsert({ id: 'p', name: 'P', thumbnail: JPEG(1) }, { image: PNG(1), layout: {} });
  kv._map.set(IMAGE_PREFIX + 'p', PNG(2));
  kv._map.set(TK, { blob: blobOfDataUrl(JPEG(3)), sig: 'peer' });
  const before = store.getMeta('p').thumbnail;
  kv.gets = 0;
  await Promise.all([backend.refresh('p'), backend.refresh('p')]);
  assert.equal(kv.gets, 3, 'payload, image, thumbnail — once');
  assert.equal(store.get('p').payload.image, PNG(2));
  assert.notEqual(store.getMeta('p').thumbnail, before);
  kv._map.delete(TK);
  await backend.refresh('p');
  assert.equal(store.getMeta('p').thumbnail, null);
});

// ── the upgrade: a browser that ran the previous build ──
const OLD_THUMB_ROWS = [
  { id: 'a', name: 'Alpha', updatedAt: 3, thumbnail: null, hasImage: true },
  { id: 'b', name: 'Beta', updatedAt: 2, thumbnail: JPEG(20), hasImage: true },
];
const oldBrowser = () => createMemoryStorage({
  [REGISTRY_KEY]: JSON.stringify(OLD_THUMB_ROWS),
  [PROJECT_PREFIX + 'a']: JSON.stringify({ image: PNG(10), layout: { imageWidth: 4 } }),
  [PROJECT_PREFIX + 'b']: JSON.stringify({ image: PNG(20), layout: { imageWidth: 5 } }),
  [THUMB_PREFIX + 'a']: JPEG(10),
});

test('an older build\'s projects all load after the upgrade, their bulk moved out of localStorage', async () => {
  const ls = oldBrowser();
  const kv = makeKv();
  const backend = await createProjectsBackend({ storage: ls, kv });
  const store = new ProjectsStore(backend);
  assert.ok(kv._map.get(THUMB_PREFIX + 'a').blob instanceof Blob, 'a localStorage thumbnail became a Blob at boot');
  assert.equal(ls._map.has(THUMB_PREFIX + 'a'), false);
  const rows = store.list();
  assert.deepEqual(rows.map((m) => m.name), ['Alpha', 'Beta']);
  assert.ok(rows.every((m) => /^blob:/.test(m.thumbnail)), 'both thumbnails show — the inline one moved on first read');
  assert.equal(await bytesOf(kv._map.get(THUMB_PREFIX + 'b').blob), 'jpeg-bytes-20');
  assert.ok(!ls.getItem(REGISTRY_KEY).includes('base64'));
  assert.deepEqual(store.get('a').payload, { image: PNG(10), layout: { imageWidth: 4 } });
  assert.deepEqual(store.get('b').payload, { image: PNG(20), layout: { imageWidth: 5 } });
  const b = store.get('b');
  store.upsert(b.meta, b.payload);
  await backend.flush();
  assert.equal(await bytesOf(kv._map.get(IMAGE_PREFIX + 'b').blob), 'png-bytes-20'.repeat(50), 'the next save moves the image to its key');
  assert.deepEqual(store.get('b').payload, { image: PNG(20), layout: { imageWidth: 5 } });
  assert.deepEqual([...ls._map.keys()], [REGISTRY_KEY]);
});
