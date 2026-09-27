// BR-7 for the image key: IndexedDB keeps a Blob, a boot holds its handle rather than the image, a
// read hands out an object URL until resolveImage turns the open project's back into its data URL,
// and an older build's image string becomes a Blob behind the boot that reads it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createProjectsBackend } from '../../../../js/core/project/store/projectsBackend.js';
import { ProjectsStore } from '../../../../js/core/project/store/projectsStore.js';
import { IMAGE_PREFIX } from '../../../../js/core/project/store/projectImages.js';
import { createMemoryStorage } from '../../../helpers/memoryStorage.js';

const makeKv = (init = {}) => {
  const m = new Map(Object.entries(init));
  const kv = {
    _map: m, sets: [],
    async get(k) { return m.has(k) ? m.get(k) : undefined; },
    async set(k, v) { kv.sets.push(k); m.set(k, v); },
    async remove(k) { m.delete(k); },
    async entries() { return Array.from(m.entries()); },
  };
  return kv;
};

const PNG = (n) => `data:image/png;base64,${btoa(`png-bytes-${n}`.repeat(50))}`;
const bytesOf = async (blob) => Buffer.from(await blob.arrayBuffer()).toString();
const boot = async (kv, ls = createMemoryStorage()) => {
  const backend = await createProjectsBackend({ storage: ls, kv });
  return { backend, store: new ProjectsStore(backend), ls };
};

test('a reload holds the Blob, reads an object URL, and resolves the open image to its data URL', async () => {
  const kv = makeKv();
  const first = await boot(kv);
  first.store.upsert({ id: 'p', name: 'P' }, { image: PNG(1), layout: {} });
  assert.equal(first.store.get('p').payload.image, PNG(1), 'the writing tab reads its own string back');
  await first.backend.flush();

  const { store, ls } = await boot(kv, first.ls);
  const url = store.get('p').payload.image;
  assert.match(url, /^blob:/);
  assert.equal(store.get('p').payload.image, url, 'one URL per Blob');
  const data = await store.resolveImage('p', url);
  assert.equal(data, PNG(1));
  assert.equal(store.get('p').payload.image, data, 'held: a save compares the same string');
  kv.sets.length = 0;
  store.upsert(store.getMeta('p'), { image: data, layout: { lines: [] } });
  assert.equal(kv.sets.includes(IMAGE_PREFIX + 'p'), false, 'an unchanged image is never rewritten');
  assert.deepEqual([...ls._map.keys()], ['stencil_projects_v1']);
});

test('one image is held at a time; an unheld read gives the data URL without taking the hold', async () => {
  const kv = makeKv();
  const first = await boot(kv);
  first.store.upsert({ id: 'a', name: 'A' }, { image: PNG(1), layout: {} });
  first.store.upsert({ id: 'b', name: 'B' }, { image: PNG(2), layout: {} });
  await first.backend.flush();
  const { store } = await boot(kv, first.ls);
  const a = await store.resolveImage('a', store.get('a').payload.image);
  assert.equal(await store.resolveImage('b', store.get('b').payload.image, false), PNG(2));
  assert.equal(store.get('a').payload.image, a, 'a keeps the hold');
  assert.match(store.get('b').payload.image, /^blob:/);
  await store.resolveImage('b', store.get('b').payload.image);
  assert.match(store.get('a').payload.image, /^blob:/, 'a let go once b was opened');
});

test('an object URL saved under another project copies its Blob, written once', async () => {
  const kv = makeKv();
  const first = await boot(kv);
  first.store.upsert({ id: 'a', name: 'A' }, { image: PNG(3), layout: {} });
  await first.backend.flush();
  const { backend, store } = await boot(kv, first.ls);
  const url = store.get('a').payload.image;
  store.upsert({ id: 'copy', name: 'Copy' }, { image: url, layout: {} });
  store.upsert({ id: 'copy', name: 'Copy' }, { image: url, layout: { lines: [] } });
  await backend.flush();
  assert.equal(kv.sets.filter((k) => k === IMAGE_PREFIX + 'copy').length, 1);
  assert.equal(kv._map.get(IMAGE_PREFIX + 'copy').blob, kv._map.get(IMAGE_PREFIX + 'a').blob);
  assert.equal(await store.resolveImage('copy', store.get('copy').payload.image), PNG(3));
});

test('an older build\'s image string still reads, and becomes a Blob behind the boot', async () => {
  const kv = makeKv({ [IMAGE_PREFIX + 'old']: PNG(4) });
  const { backend } = await boot(kv);
  assert.equal(backend.getItem(IMAGE_PREFIX + 'old'), PNG(4));
  await backend.flush();
  assert.equal(await bytesOf(kv._map.get(IMAGE_PREFIX + 'old').blob), 'png-bytes-4'.repeat(50));
  const again = await boot(kv);
  assert.match(again.backend.getItem(IMAGE_PREFIX + 'old'), /^blob:/);
});

test('a second image written before the first decodes leaves only the second in IndexedDB', async () => {
  const kv = makeKv();
  const { backend, store } = await boot(kv);
  store.upsert({ id: 'p', name: 'P' }, { image: PNG(5), layout: {} });
  store.upsert({ id: 'p', name: 'P' }, { image: PNG(6), layout: {} });
  await backend.flush();
  assert.equal(await bytesOf(kv._map.get(IMAGE_PREFIX + 'p').blob), 'png-bytes-6'.repeat(50));
  assert.equal(kv.sets.filter((k) => k === IMAGE_PREFIX + 'p').length, 1);
  store.remove('p');
  await backend.flush();
  assert.equal(kv._map.has(IMAGE_PREFIX + 'p'), false);
});
