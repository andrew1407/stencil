// js/core/storage/storage.js: the cross-tab UPDATED for a save goes out only once that save's
// IndexedDB write has committed — another tab reads IndexedDB — and never for a write that failed.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mountStorage } from '../../helpers/storageRig.js';
import { createMemoryStorage } from '../../helpers/memoryStorage.js';
import { createProjectsBackend } from '../../../js/core/project/store/projectsBackend.js';
import { ProjectsStore } from '../../../js/core/project/store/projectsStore.js';

const flush = () => new Promise((r) => setImmediate(r));

// Every KV write waits for its gate; `release(err)` settles the oldest one.
const gatedRig = async (t) => {
  const gates = [];
  const kv = {
    async get() {}, async remove() {}, async entries() { return []; },
    set: () => new Promise((resolve, reject) => gates.push((err) => (err ? reject(err) : resolve()))),
  };
  const backend = await createProjectsBackend({ storage: createMemoryStorage(), kv });
  const sent = [];
  const rig = mountStorage(t, { image: false, app: {
    tabs: { reportActive() {}, reportIncognito() {}, projectsChanged: (d) => sent.push(d ?? null) } } });
  Object.assign(rig.storage, { backend, store: new ProjectsStore(backend), activeId: 'p1', temporary: false });
  rig.app.activeProjectId = 'p1';
  const release = async (err) => { while (gates.length) gates.shift()(err); await flush(); await flush(); };
  return { ...rig, sent, release };
};

test('the broadcast waits for the commit, then goes out once', async (t) => {
  const { storage, sent, release } = await gatedRig(t);
  storage.save();
  storage.save();
  t.mock.timers.tick(400); await flush();
  assert.deepEqual(sent, [], 'nothing announced while the write is in flight');
  await release();
  assert.deepEqual(sent, [{ id: 'p1', action: 'updated' }]);
});

test('a save whose payload never committed announces no update', async (t) => {
  const { storage, sent, release } = await gatedRig(t);
  storage.save();
  t.mock.timers.tick(400); await flush();
  await release(new Error('idb broken'));
  assert.ok(!sent.some((d) => d?.action === 'updated'));
  assert.equal(storage.store.getMeta('p1'), null, 'the orphaned row went with it');
});
