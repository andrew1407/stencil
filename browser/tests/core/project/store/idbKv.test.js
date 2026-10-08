// js/core/project/store/idbKv.js over a fake IDBFactory: a write settles with its transaction, so a
// quota abort at commit time — after the put request itself succeeded — rejects the write.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createIdbKv } from '../../../../js/core/project/store/idbKv.js';

// Each transaction fires its request's success, then completes or aborts as `commit()` says.
const fakeIdb = (commit) => {
  const data = new Map();
  const request = (result) => {
    const req = { result };
    queueMicrotask(() => req.onsuccess?.());
    return req;
  };
  const db = {
    createObjectStore() {},
    transaction() {
      const tx = {};
      tx.objectStore = () => ({
        get: (k) => request(data.get(k)),
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
