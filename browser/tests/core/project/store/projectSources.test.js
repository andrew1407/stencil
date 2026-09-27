// A data-URL image source never sits in the localStorage registry: the row keeps a short, stable
// reference, the full URL stays in the project's payload (IndexedDB), and it is read back from
// there only where a caller needs the text. Delete, expiry and a re-point leave no copy behind.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ProjectsStore, REGISTRY_KEY, PROJECT_PREFIX } from '../../../../js/core/project/store/projectsStore.js';
import { createProjectsBackend } from '../../../../js/core/project/store/projectsBackend.js';
import {
  SOURCE_REF_SCHEME, sourceRef, isSourceRef, storedSource, resolveSource, shedSource, wireSource,
} from '../../../../js/core/project/store/projectSources.js';
import { upsertWithQuota } from '../../../../js/core/storage/quotaWriter.js';
import { openInLaunchPayload } from '../../../../js/core/launch/payload.js';
import { createMemoryStorage } from '../../../helpers/memoryStorage.js';

const BOUND = 128;
const dataUrl = (fill, n = 200000) => `data:image/png;base64,${fill.repeat(n)}`;
const [A, B, C] = ['A', 'B', 'C'].map((f) => dataUrl(f));
const fresh = (s) => [...s].join('');
const IMG = 'data:image/jpeg;base64,/9j/IMG';

const makeKv = (init = {}, opts = {}) => {
  const m = new Map(Object.entries(init));
  return {
    _map: m,
    async get(k) { return m.has(k) ? m.get(k) : undefined; },
    async set(k, v) { if (opts.failSet) throw new Error('idb set'); m.set(k, v); },
    async remove(k) { m.delete(k); },
    async entries() { return Array.from(m.entries()); },
  };
};

const quota = () => Object.assign(new Error('quota'), { name: 'QuotaExceededError' });
// localStorage with a character budget: a write that would take the whole over `cap` throws.
const capped = (cap, init = {}) => {
  const s = createMemoryStorage(init);
  const set = s.setItem.bind(s);
  s.setItem = (k, v) => {
    let used = 0;
    for (const [key, val] of s._map) if (key !== k) used += key.length + val.length;
    if (used + k.length + String(v).length > cap) throw quota();
    set(k, v);
  };
  return s;
};

const save = (store, id, source, extra = {}) =>
  store.upsert({ id, name: id, thumbnail: null, source, ...extra }, { image: IMG, layout: { imageSource: source } });
const holds = (map, url) => [...map.values()].some((v) => String(v).includes(url.slice(-64)));
const rowSources = (ls) => JSON.parse(ls.getItem(REGISTRY_KEY)).map((m) => m.source);
const withIdb = async (kvInit) => {
  const ls = createMemoryStorage();
  const kv = makeKv(kvInit);
  const backend = await createProjectsBackend({ storage: ls, kv });
  return { ls, kv, backend, store: new ProjectsStore(backend) };
};

test('the reference is short, stable across string copies and tabs, and maps to itself', () => {
  const ref = sourceRef(A);
  assert.ok(isSourceRef(ref) && ref.startsWith(`${SOURCE_REF_SCHEME}image/png;`) && ref.length < BOUND);
  assert.equal(sourceRef(fresh(A)), ref);
  assert.notEqual(sourceRef(A.slice(0, -1) + 'B'), ref, 'one character apart');
  assert.equal(sourceRef(ref), ref);
  assert.equal(sourceRef('https://x/a.png'), 'https://x/a.png');
  assert.equal(sourceRef(null), null);
});

test('a save keeps the reference in the registry and the full URL in the IndexedDB payload', async () => {
  const { ls, kv, backend, store } = await withIdb();
  save(store, 'p1', A);
  save(store, 'p2', 'https://x/b.png');
  await backend.flush();
  assert.ok(!ls.getItem(REGISTRY_KEY).includes('data:'), 'no data URL in the registry');
  assert.ok(rowSources(ls).every((s) => s.length < BOUND));
  assert.deepEqual(rowSources(ls), [sourceRef(A), 'https://x/b.png']);
  assert.ok(!holds(ls._map, A), 'nothing in localStorage holds it');
  assert.ok(holds(kv._map, A), 'the payload does');
  assert.equal(store.getMeta('p1').source, sourceRef(A));
  assert.equal(storedSource(store, 'p1'), A, 'resolved from the payload');
  assert.equal(storedSource(new ProjectsStore(backend), 'p1'), A, 'and from a peer tab');
  assert.equal(storedSource(store, 'p2'), 'https://x/b.png');
  assert.equal(storedSource(store, 'nope'), null);
});

test('opening the same image again still finds its project; another image does not', async () => {
  const { store } = await withIdb();
  save(store, 'photo', A);
  assert.deepEqual(store.findByImage(fresh(A), 'whatever').map((m) => m.id), ['photo']);
  assert.equal(store.copyName('photo', fresh(A)), 'photo (1)');
  assert.deepEqual(store.findByImage(B, 'photo'), []);
  assert.equal(store.copyName('photo', B), 'photo', 'a different image is not a copy');
});

test('a saved project handed off to another editor carries its full source', async () => {
  const { store } = await withIdb();
  save(store, 'p1', A);
  const handoff = openInLaunchPayload({ activeProjectId: null, storage: { store } }, { id: 'p1' });
  assert.equal(handoff.source, A);
  assert.equal(handoff.dataUrl, IMG);
});

test('delete, expiry and a re-point leave no copy of the data URL anywhere', async () => {
  const { ls, kv, backend, store } = await withIdb();
  save(store, 'gone', A);
  save(store, 'old', B, { expiresAt: 1 });
  save(store, 'moved', C);
  store.remove('gone');
  assert.deepEqual(store.sweepExpired(Date.now()), ['old']);
  const { meta, payload } = store.get('moved');
  store.upsert({ ...meta, source: 'https://x/c.png' }, { ...payload, layout: { ...payload.layout, imageSource: 'https://x/c.png' } });
  await backend.flush();
  for (const url of [A, B, C]) assert.ok(!holds(kv._map, url) && !holds(ls._map, url));
  assert.equal(storedSource(store, 'moved'), 'https://x/c.png');
});

test('an older registry with inline data-URL sources moves them out on the first read', async () => {
  const rows = [{ id: 'a', name: 'a', source: A, updatedAt: 3 }, { id: 'b', name: 'b', source: 'https://x/b', updatedAt: 2 },
    { id: 'c', name: 'c', source: B, updatedAt: 1 }];
  const { ls, kv, backend } = await withIdb({
    [PROJECT_PREFIX + 'a']: JSON.stringify({ image: IMG, layout: { imageSource: A } }),
    [PROJECT_PREFIX + 'c']: JSON.stringify({ image: IMG, layout: {} }),
  });
  ls.setItem(REGISTRY_KEY, JSON.stringify(rows));
  const store = new ProjectsStore(backend);
  assert.deepEqual(store.list().map((m) => m.source), [sourceRef(A), 'https://x/b', sourceRef(B)]);
  await backend.flush();
  assert.ok(!ls.getItem(REGISTRY_KEY).includes('data:'));
  assert.ok(holds(kv._map, B), 'the payload that lacked the text now holds it');
  assert.equal(storedSource(store, 'a'), A);
  assert.equal(storedSource(store, 'c'), B);
  assert.deepEqual(store.findByImage(A, '').map((m) => m.id), ['a']);
  const raw = ls.getItem(REGISTRY_KEY);
  new ProjectsStore(backend).list();
  assert.equal(ls.getItem(REGISTRY_KEY), raw, 'a second read moves nothing');
});

test('without IndexedDB the registry still keeps only the reference', async () => {
  const ls = createMemoryStorage();
  const store = new ProjectsStore(await createProjectsBackend({ storage: ls, kv: null }));
  save(store, 'p1', A);
  assert.ok(!ls.getItem(REGISTRY_KEY).includes('data:'));
  assert.equal(storedSource(store, 'p1'), A, 'the payload, in localStorage here, still holds it');
});

test('a payload that cannot take the text sheds it: the reference, image and project survive', () => {
  const ls = capped(300000, {
    [REGISTRY_KEY]: JSON.stringify([{ id: 'p1', name: 'p1', source: A, updatedAt: 1 }]),
    [PROJECT_PREFIX + 'p1']: JSON.stringify({ image: IMG, layout: {} }),
  });
  const store = new ProjectsStore(ls);
  assert.equal(store.getMeta('p1').source, sourceRef(A));
  assert.ok(!ls.getItem(REGISTRY_KEY).includes('data:'));
  assert.equal(store.get('p1').payload.image, IMG);
  assert.equal(storedSource(store, 'p1'), sourceRef(A), 'the full text is what is lost');
  assert.deepEqual(store.findByImage(A, '').map((m) => m.id), ['p1'], 'dedupe is not');
});

test('an IndexedDB write failure reaches the status line, never the caller', async () => {
  const kv = makeKv({ [PROJECT_PREFIX + 'p1']: JSON.stringify({ image: IMG, layout: {} }) }, { failSet: true });
  const ls = createMemoryStorage({ [REGISTRY_KEY]: JSON.stringify([{ id: 'p1', name: 'p1', source: A }]) });
  const backend = await createProjectsBackend({ storage: ls, kv });
  const errors = [];
  backend.onWriteError = (e) => errors.push(e);
  const store = new ProjectsStore(backend);
  assert.equal(store.getMeta('p1').source, sourceRef(A));
  await backend.flush();
  assert.equal(errors.length, 1);
  assert.ok(!ls.getItem(REGISTRY_KEY).includes('data:'));
  const reloaded = new ProjectsStore(await createProjectsBackend({ storage: ls, kv: makeKv(Object.fromEntries(kv._map)) }));
  assert.equal(reloaded.get('p1').payload.image, IMG, 'after a reload the image is intact');
  assert.equal(storedSource(reloaded, 'p1'), sourceRef(A));
});

test('a save over the quota sheds the source text before the image or another project', () => {
  const ls = capped(150000);
  const store = new ProjectsStore(ls);
  save(store, 'other', 'https://x/o.png');
  const statuses = [];
  const io = {
    store, activeId: 'p1', showImageMissingBanner: () => {},
    app: { showSaveStatus: (t) => statuses.push(t), originalImage: null, image: null },
  };
  upsertWithQuota(io, { id: 'p1', name: 'p1', thumbnail: null, source: A }, { image: IMG, layout: { imageSource: A } });
  assert.deepEqual(statuses, ['Saved']);
  assert.equal(store.get('p1').payload.image, IMG);
  assert.equal(store.get('p1').payload.layout.imageSource, sourceRef(A));
  assert.ok(store.getMeta('other'), 'nothing evicted');
});

test('shed, resolve and the server wire', () => {
  const plain = { image: IMG, layout: { imageSource: 'https://x/a.png' } };
  assert.equal(shedSource(plain), plain);
  assert.equal(shedSource({ image: IMG, layout: { imageSource: A } }).layout.imageSource, sourceRef(A));
  assert.equal(resolveSource(sourceRef(A), B), sourceRef(A), 'another image never resolves it');
  assert.equal(resolveSource('https://x', A), 'https://x');
  assert.equal(wireSource(A), '');
  assert.equal(wireSource(sourceRef(A)), '');
  assert.equal(wireSource('https://x/a.png'), 'https://x/a.png');
  assert.equal(wireSource(null), '');
});
