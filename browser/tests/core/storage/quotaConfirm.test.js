// js/core/storage/quotaConfirm.js over the IndexedDB backend with a stubbed KV: a write that fails
// after the synchronous upsert resumes the quota ladder, and a payload that never committed takes
// its registry row with it rather than leaving a listed project that cannot open.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createProjectsBackend } from '../../../js/core/project/store/projectsBackend.js';
import { ProjectsStore, PROJECT_PREFIX } from '../../../js/core/project/store/projectsStore.js';
import { IMAGE_PREFIX } from '../../../js/core/project/store/projectImages.js';
import { upsertWithQuota } from '../../../js/core/storage/quotaWriter.js';
import { confirmCommit } from '../../../js/core/storage/quotaConfirm.js';
import { createMemoryStorage } from '../../helpers/memoryStorage.js';

const quota = () => Object.assign(new Error('quota'), { name: 'QuotaExceededError' });
// `arm(refuse)`: refuse(key) is the error a write of that key then fails with, asynchronously, or null.
const rig = async () => {
  const m = new Map();
  let refuse = () => null;
  const kv = {
    async get(k) { return m.get(k); },
    async set(k, v) { const e = refuse(k); if (e) throw e; m.set(k, v); },
    async remove(k) { m.delete(k); },
    async entries() { return Array.from(m.entries()); },
  };
  const backend = await createProjectsBackend({ storage: createMemoryStorage(), kv });
  const statuses = [];
  let changes = 0;
  const io = {
    backend, store: new ProjectsStore(backend), activeId: 'p1', showImageMissingBanner() {},
    app: { showSaveStatus: (t) => statuses.push(t), originalImage: null, image: null,
      tabs: { projectsChanged: () => { changes++; } } },
  };
  const arm = (fn) => { refuse = fn; };
  return { io, statuses, arm, changes: () => changes };
};

const IMG = `data:image/png;base64,${btoa('png'.repeat(40))}`;
const save = (io, id, image = null) =>
  confirmCommit(io, id, upsertWithQuota(io, { id, name: id, thumbnail: null }, { image, layout: { lines: [] } }));

test('an image the backend refuses for quota resumes the ladder: evict, then lines only', async () => {
  const { io, statuses, arm } = await rig();
  assert.equal(await save(io, 'old'), true);
  arm((k) => (k === IMAGE_PREFIX + 'p1' ? quota() : null));
  assert.equal(await save(io, 'p1', IMG), true);
  assert.equal(io.store.getMeta('old'), null, 'the oldest other project made room');
  assert.equal(statuses.at(-1), 'Lines saved — image too large for browser storage');
  assert.equal(io.store.get('p1').payload.image, null);
  assert.ok(io.backend.isCommitted(PROJECT_PREFIX + 'p1'));
});

test('a new project whose payload never commits is not left listed', async () => {
  const { io, arm, changes } = await rig();
  arm((k) => (k === PROJECT_PREFIX + 'p1' ? new Error('idb broken') : null));
  assert.equal(await save(io, 'p1'), false);
  assert.equal(io.store.getMeta('p1'), null, 'no registry row without a payload');
  assert.equal(changes(), 1, 'the other tabs re-list');
});

test('a committed project keeps its row when a later write fails: it still opens', async () => {
  const { io, arm } = await rig();
  assert.equal(await save(io, 'p1'), true);
  arm((k) => (k === PROJECT_PREFIX + 'p1' ? new Error('idb broken') : null));
  assert.equal(await save(io, 'p1'), false);
  assert.ok(io.store.getMeta('p1'));
});

test('only the newest save confirms; an older one reports itself superseded', async () => {
  const { io } = await rig();
  const first = save(io, 'p1');
  const second = save(io, 'p1');
  assert.deepEqual([await first, await second], [false, true]);
});
