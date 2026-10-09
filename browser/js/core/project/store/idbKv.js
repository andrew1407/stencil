// The promise KV over the one IndexedDB object store that holds the project payloads, images and
// thumbnails (database stencil_projects, store payloads, keyed by the localStorage-shaped key).

const PROJECTS_DB_NAME = 'stencil_projects';
const PROJECTS_DB_STORE = 'payloads';

// Minimal promise KV over one object store (store.js's createIdbBackend shape plus the
// bulk entries() read). Null when IndexedDB is missing.
export const createIdbKv = (idb = (typeof indexedDB !== 'undefined' ? indexedDB : null)) => {
  if (!idb) return null;
  let dbPromise = null;
  const openDb = () => new Promise((resolve, reject) => {
    const req = idb.open(PROJECTS_DB_NAME, 1);
    req.onupgradeneeded = () => { req.result.createObjectStore(PROJECTS_DB_STORE); };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  const db = () => (dbPromise ||= openDb());
  // A write settles with its transaction: a quota failure aborts at commit, after the request succeeded.
  const op = (mode, run) => db().then((d) => new Promise((resolve, reject) => {
    const tx = d.transaction(PROJECTS_DB_STORE, mode);
    const req = run(tx.objectStore(PROJECTS_DB_STORE));
    req.onerror = () => reject(req.error);
    if (mode === 'readonly') { req.onsuccess = () => resolve(req.result); return; }
    tx.oncomplete = () => resolve(req.result);
    tx.onabort = () => reject(tx.error || req.error);
  }));
  // Keys and values from ONE transaction: two would let another tab's write between them misalign the pairs.
  const entries = () => db().then((d) => new Promise((resolve, reject) => {
    const store = d.transaction(PROJECTS_DB_STORE, 'readonly').objectStore(PROJECTS_DB_STORE);
    const keys = store.getAllKeys();
    const values = store.getAll();
    keys.onerror = () => reject(keys.error);
    values.onerror = () => reject(values.error);
    values.onsuccess = () => resolve(keys.result.map((k, i) => [k, values.result[i]]));
  }));
  return {
    get: (key) => op('readonly', (s) => s.get(key)),
    set: (key, value) => op('readwrite', (s) => s.put(value, key)),
    remove: (key) => op('readwrite', (s) => s.delete(key)),
    entries,
  };
};
