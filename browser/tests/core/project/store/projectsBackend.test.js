import { test } from 'node:test';
import assert from 'node:assert/strict';

import { createProjectsBackend } from '../../../../js/core/project/store/projectsBackend.js';
import { ProjectsStore, REGISTRY_KEY, PROJECT_PREFIX } from '../../../../js/core/project/store/projectsStore.js';
import { createMemoryStorage } from '../../../helpers/memoryStorage.js';

// The backend splits ProjectsStore's keys across two homes: payload keys
// (stencil_project_<id>) ride an in-memory mirror written through to IndexedDB,
// everything else (registry, flags, legacy keys) passes through to localStorage.
// Both homes are injected: a Map-backed localStorage shim (projectsStore.test.js's
// idiom) and an async Map-backed KV standing in for IndexedDB (chatStore.test.js's).

const makeShim = (init = {}) => createMemoryStorage(init);

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

const PK = PROJECT_PREFIX + 'p1';

test('routes payloads to the KV and everything else to localStorage', async () => {
  const ls = makeShim();
  const kv = makeKv();
  const backend = await createProjectsBackend({ storage: ls, kv });

  backend.setItem(REGISTRY_KEY, '[]');
  backend.setItem(PK, '{"image":null}');
  await backend.flush();

  assert.equal(ls._map.get(REGISTRY_KEY), '[]');
  assert.equal(ls._map.has(PK), false);          // payloads never touch localStorage
  assert.equal(kv._map.get(PK), '{"image":null}');
  assert.equal(kv._map.has(REGISTRY_KEY), false); // registry never touches the KV
  assert.equal(backend.getItem(PK), '{"image":null}');
  assert.deepEqual(new Set(backend.keys()), new Set([REGISTRY_KEY, PK]));

  backend.removeItem(PK);
  await backend.flush();
  assert.equal(backend.getItem(PK), null);
  assert.equal(kv._map.has(PK), false);
});

test('hydrates the mirror from the KV so payload reads are sync from boot', async () => {
  const kv = makeKv({ [PK]: '{"image":"data:x"}' });
  const backend = await createProjectsBackend({ storage: makeShim(), kv });
  assert.equal(backend.getItem(PK), '{"image":"data:x"}');
});

test('migrates localStorage payloads into the KV and frees the quota', async () => {
  const ls = makeShim({
    [REGISTRY_KEY]: JSON.stringify([{ id: 'p1', name: 'A', updatedAt: 1 }]),
    [PK]: '{"image":"data:old"}',
  });
  const kv = makeKv();
  const backend = await createProjectsBackend({ storage: ls, kv });

  assert.equal(kv._map.get(PK), '{"image":"data:old"}'); // copied…
  assert.equal(ls._map.has(PK), false);                  // …then deleted from localStorage
  assert.equal(ls._map.has(REGISTRY_KEY), true);         // registry stays put
  assert.equal(backend.getItem(PK), '{"image":"data:old"}');
});

test('migration failure keeps the localStorage copy and still serves the payload', async () => {
  const ls = makeShim({ [PK]: '{"image":"data:keep"}' });
  const backend = await createProjectsBackend({ storage: ls, kv: makeKv({}, { failSet: true }) });

  assert.equal(ls._map.has(PK), true);                   // not deleted — retried next boot
  assert.equal(backend.getItem(PK), '{"image":"data:keep"}');
});

test('refresh(id) pulls another tab’s KV write into the mirror (and drops deletions)', async () => {
  const kv = makeKv({ [PK]: 'v1' });
  const backend = await createProjectsBackend({ storage: makeShim(), kv });

  kv._map.set(PK, 'v2');                 // another tab wrote
  assert.equal(backend.getItem(PK), 'v1'); // mirror is per-tab until refreshed
  await backend.refresh('p1');
  assert.equal(backend.getItem(PK), 'v2');

  kv._map.delete(PK);                    // another tab removed the project
  await backend.refresh('p1');
  assert.equal(backend.getItem(PK), null);
});

test('async write failure reaches onWriteError, not the sync caller', async () => {
  const backend = await createProjectsBackend({ storage: makeShim(), kv: makeKv({}, { failSet: true }) });
  const errors = [];
  backend.onWriteError = e => errors.push(e);

  backend.setItem(PK, 'v');              // must not throw
  await backend.flush();
  assert.equal(errors.length, 1);
  assert.equal(backend.getItem(PK), 'v'); // the mirror still serves the bytes
});

test('degrades to the plain storage object when IndexedDB is unavailable', async () => {
  const ls = makeShim();
  assert.equal(await createProjectsBackend({ storage: ls, kv: null }), ls);
  const rejecting = { entries: async () => { throw new Error('open blocked'); } };
  assert.equal(await createProjectsBackend({ storage: ls, kv: rejecting }), ls);
});

test('ProjectsStore round-trips over the backend, clearAll included', async () => {
  const ls = makeShim();
  const kv = makeKv();
  const store = new ProjectsStore(await createProjectsBackend({ storage: ls, kv }));

  const meta = store.upsert({ id: 'p1', name: 'Photo' }, { image: 'data:img', layout: {} });
  assert.equal(meta.id, 'p1');
  assert.deepEqual(store.get('p1').payload, { image: 'data:img', layout: {} });
  assert.equal(ls._map.has(PK), false);  // the heavy payload stayed out of localStorage

  store.clearAll();
  assert.equal(store.get('p1'), null);
  assert.equal(store.list().length, 0);
});
