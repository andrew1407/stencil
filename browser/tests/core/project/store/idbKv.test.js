// js/core/project/store/idbKv.js over a fake IDBFactory: a write settles with its transaction, so a
// quota abort at commit time — after the put request itself succeeded — rejects the write, and the
// bulk read pairs keys with values from one transaction's snapshot.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createIdbKv } from '../../../../js/core/project/store/idbKv.js';

// Each transaction fires its request's success, then completes or aborts as `commit()` says.
const fakeIdb = (commit, { data = new Map(), opened = () => {} } = {}) => {
  const request = (result) => {
    const req = { result };
    queueMicrotask(() => req.onsuccess?.());
    return req;
  };
  const db = {
    createObjectStore() {},
    transaction() {
      const tx = {};
      // A transaction reads the store as it stood when it opened, as IndexedDB's isolation does.
      const snap = new Map([...data].sort(([a], [b]) => (a < b ? -1 : 1)));
      opened();
      tx.objectStore = () => ({
        get: (k) => request(snap.get(k)),
        getAllKeys: () => request([...snap.keys()]),
        getAll: () => request([...snap.values()]),
        put: (v, k) => {
          const req = request(k);
          queueMicrotask(() => queueMicrotask(() => {
            const err = commit(k);
            if (err) { tx.error = err; tx.onabort?.(); } else { data.set(k, v); tx.oncomplete?.(); }
          }));
          return req;
        },
      });
      return tx;
    },
  };
  return { open: () => request(db) };
};

test('a write resolves once its transaction commits', async () => {
  const kv = createIdbKv(fakeIdb(() => null));
  await kv.set('stencil_project_a', '{}');
  assert.equal(await kv.get('stencil_project_a'), '{}');
});

test('a quota abort at commit rejects the write the request had already answered', async () => {
  const quota = Object.assign(new Error('quota'), { name: 'QuotaExceededError' });
  const kv = createIdbKv(fakeIdb(() => quota));
  await assert.rejects(kv.set('stencil_project_a', '{}'), (e) => e === quota);
  assert.equal(await kv.get('stencil_project_a'), undefined);
});

test('no IndexedDB, no KV', () => {
  assert.equal(createIdbKv(null), null);
});

test('entries pairs keys and values from one transaction, whatever another tab writes meanwhile', async () => {
  const data = new Map([['stencil_project_b', 'B']]);
  let txs = 0;
  // Another tab's write lands right after the first transaction opens.
  const kv = createIdbKv(fakeIdb(() => null, { data, opened: () => { if (++txs === 1) data.set('stencil_project_a', 'A'); } }));
  assert.deepEqual(await kv.entries(), [['stencil_project_b', 'B']]);
  assert.equal(txs, 1, 'one transaction for the keys and the values');
  assert.deepEqual(await kv.entries(), [['stencil_project_a', 'A'], ['stencil_project_b', 'B']]);
});
