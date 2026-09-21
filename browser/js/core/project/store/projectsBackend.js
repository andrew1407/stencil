// IndexedDB payload storage behind ProjectsStore's SYNC localStorage-shaped contract, the
// parity twin of core/state/ProjectsStore.cpp. Only the per-project payload keys
// (stencil_project_<id>) move to IndexedDB, via an in-memory mirror hydrated once at boot
// (initProjectsBackend, awaited by index.js) and written through asynchronously. The registry,
// migration flag and legacy keys stay in localStorage for cross-tab reads and the extension.

import { PROJECT_PREFIX } from './projectsStore.js';

const PROJECTS_DB_NAME = 'stencil_projects';
const PROJECTS_DB_STORE = 'payloads';

// Minimal promise KV over one object store (store.js's createIdbBackend shape plus the
// bulk entries() read). Null when IndexedDB is missing.
const createIdbKv = (idb = (typeof indexedDB !== 'undefined' ? indexedDB : null)) => {
  if (!idb) return null;
  let dbPromise = null;
  const openDb = () => new Promise((resolve, reject) => {
    const req = idb.open(PROJECTS_DB_NAME, 1);
    req.onupgradeneeded = () => { req.result.createObjectStore(PROJECTS_DB_STORE); };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  const db = () => (dbPromise ||= openDb());
  const op = (mode, run) => db().then((d) => new Promise((resolve, reject) => {
    const tx = d.transaction(PROJECTS_DB_STORE, mode);
    const req = run(tx.objectStore(PROJECTS_DB_STORE));
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  }));
  return {
    get: (key) => op('readonly', (s) => s.get(key)),
    set: (key, value) => op('readwrite', (s) => s.put(value, key)),
    remove: (key) => op('readwrite', (s) => s.delete(key)),
    entries: () => Promise.all([
      op('readonly', (s) => s.getAllKeys()),
      op('readonly', (s) => s.getAll()),
    ]).then(([keys, values]) => keys.map((k, i) => [k, values[i]])),
  };
};

// `kv` is injectable for tests; when IndexedDB is unusable the plain `storage` is returned
// as-is — exactly the pre-IndexedDB behaviour.
export const createProjectsBackend = async ({ storage, idb, kv } = {}) => {
  const ls = storage !== undefined ? storage
    : (typeof localStorage !== 'undefined' ? localStorage : null);
  const store = kv !== undefined ? kv : createIdbKv(idb);
  if (!store) return ls;

  const isPayload = (k) => typeof k === 'string' && k.startsWith(PROJECT_PREFIX);
  const mirror = new Map();

  let stored;
  try {
    stored = await store.entries();
  } catch {
    return ls;
  }
  for (const [k, v] of stored) if (isPayload(k)) mirror.set(k, v);

  const lsKeys = () => {
    if (!ls) return [];
    if (typeof ls.keys === 'function') return Array.from(ls.keys());
    try { return Object.keys(ls); } catch { return []; }
  };

  // One-time migration out of localStorage. localStorage wins over a stale IndexedDB copy,
  // and each key is copied — awaited — before its twin is deleted.
  for (const k of lsKeys()) {
    if (!isPayload(k)) continue;
    const v = ls.getItem(k);
    if (v == null) continue;
    try {
      await store.set(k, v);
    } catch {
      mirror.set(k, v);
      continue;
    }
    mirror.set(k, v);
    try { ls.removeItem(k); } catch { /* stays for the next boot's retry — harmless */ }
  }

  // In-flight writes, so flush() can await persistence. The tracked twin swallows
  // rejections (callers attach their own .catch), so bookkeeping never leaks a rejection.
  const pending = new Set();
  const track = (p) => {
    const settled = p.catch(() => {});
    pending.add(settled);
    settled.then(() => pending.delete(settled));
    return p;
  };

  const backend = {
    // Storage points this at the save-status line.
    onWriteError: null,

    getItem(k) {
      if (!isPayload(k)) return ls ? ls.getItem(k) : null;
      const v = mirror.get(k);
      // A payload a failed migration left behind is still in localStorage.
      return v !== undefined ? v : (ls ? ls.getItem(k) : null);
    },
    setItem(k, v) {
      if (!isPayload(k)) {
        if (ls) ls.setItem(k, v);
        return;
      }
      const s = String(v);
      mirror.set(k, s);
      track(store.set(k, s)).catch((e) => { try { backend.onWriteError?.(e); } catch { /* status line gone */ } });
    },
    removeItem(k) {
      if (!isPayload(k)) {
        if (ls) ls.removeItem(k);
        return;
      }
      mirror.delete(k);
      track(store.remove(k)).catch(() => { /* registry entry is the source of truth */ });
      try { ls?.removeItem(k); } catch { /* no leftover to clean */ }
    },
    keys() {
      const out = new Set(mirror.keys());
      for (const k of lsKeys()) out.add(k);
      return Array.from(out);
    },

    // Re-read from IndexedDB after another tab wrote: one key when an id is given, the whole
    // mirror otherwise. Best-effort — a failed refresh leaves the mirror stale.
    async refresh(id) {
      try {
        if (id != null) {
          const key = PROJECT_PREFIX + id;
          const v = await store.get(key);
          if (v === undefined) mirror.delete(key);
          else mirror.set(key, v);
          return;
        }
        const all = await store.entries();
        mirror.clear();
        for (const [k, v] of all) if (isPayload(k)) mirror.set(k, v);
      } catch { /* stale mirror beats a throw */ }
    },

    flush: () => Promise.allSettled(Array.from(pending)).then(() => {}),
  };
  return backend;
};

// Before init — or under `node --test`, which never inits — getProjectsBackend falls back
// to plain localStorage.
let active = null;

export const initProjectsBackend = async (opts) => {
  active = await createProjectsBackend(opts);
  return active;
};

export const getProjectsBackend = () =>
  active ?? (typeof localStorage !== 'undefined' ? localStorage : null);
