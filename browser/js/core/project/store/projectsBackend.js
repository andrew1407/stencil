// IndexedDB project storage behind ProjectsStore's SYNC localStorage-shaped contract, the
// parity twin of core/state/ProjectsStore.cpp. The per-project keys — payload, image and thumbnail
// (Blobs, read back as object URLs: imageBlobs.js, thumbBlobs.js) — move to IndexedDB through an
// in-memory mirror hydrated once at boot (initProjectsBackend, awaited by index.js) and written
// through asynchronously. The registry and flags stay in localStorage for cross-tab reads.
import { PROJECT_PREFIX } from './projectsStore.js';
import { IMAGE_PREFIX } from './projectImages.js';
import { THUMB_PREFIX } from './projectThumbs.js';
import { createThumbMirror, isDataUrl, isThumbRecord } from './thumbBlobs.js';
import { createImageMirror } from './imageBlobs.js';

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
  const isImage = (k) => typeof k === 'string' && k.startsWith(IMAGE_PREFIX);
  const isThumb = (k) => typeof k === 'string' && k.startsWith(THUMB_PREFIX);
  const isOwn = (k) => isPayload(k) || isImage(k) || isThumb(k);
  const mirror = new Map();
  const images = createImageMirror();
  const thumbs = createThumbMirror();
  const adopt = (k, v) => {
    if (isPayload(k)) mirror.set(k, v);
    else if (isImage(k)) images.adopt(k, v);
    else if (isThumb(k) && isThumbRecord(v)) thumbs.adopt(k, v);
  };

  let stored;
  try {
    stored = await store.entries();
  } catch {
    return ls;
  }
  for (const [k, v] of stored) adopt(k, v);

  const lsKeys = () => {
    if (!ls) return [];
    if (typeof ls.keys === 'function') return Array.from(ls.keys());
    try { return Object.keys(ls); } catch { return []; }
  };

  // One-time migration out of localStorage (an image's or thumbnail's data URL becomes its Blob).
  // localStorage wins over a stale IndexedDB copy, and each key is copied — awaited — first.
  for (const k of lsKeys()) {
    if (!isOwn(k)) continue;
    const v = ls.getItem(k);
    if (v == null || (!isPayload(k) && !isDataUrl(v))) continue;
    let record = v;
    if (isThumb(k)) { thumbs.drop(k); record = thumbs.put(k, v); }
    else if (isImage(k)) { images.drop(k); record = await images.put(k, v).catch(() => null); }
    else mirror.set(k, v);
    try {
      if (record == null) continue;
      await store.set(k, record);
    } catch {
      continue;
    }
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

  const writeFailed = (e) => { try { backend.onWriteError?.(e); } catch { /* status line gone */ } };
  // Best-effort: a failed refresh leaves the mirror stale rather than throwing.
  const refreshing = new Map();
  const forget = (k) => {
    if (isThumb(k)) thumbs.drop(k);
    else if (isImage(k)) images.drop(k);
    else mirror.delete(k);
  };
  const refreshOne = async (id) => {
    try {
      for (const key of [PROJECT_PREFIX + id, IMAGE_PREFIX + id, THUMB_PREFIX + id]) {
        const v = await store.get(key);
        if (v !== undefined) adopt(key, v);
        else forget(key);
      }
    } catch { /* stale mirror beats a throw */ }
  };
  const refreshAll = async () => {
    try {
      const all = await store.entries();
      mirror.clear();
      for (const k of [...thumbs.keys(), ...images.keys()]) forget(k);
      for (const [k, v] of all) adopt(k, v);
    } catch { /* stale mirror beats a throw */ }
  };
  // What IndexedDB is to hold for an image once its Blob is decoded; a superseded one writes nothing.
  const persistImage = (k, pending) => {
    if (pending) track(pending.then((record) => record != null && store.set(k, record))).catch(writeFailed);
  };
  const backend = {
    // Storage points this at the save-status line.
    onWriteError: null,

    getItem(k) {
      if (!isOwn(k)) return ls ? ls.getItem(k) : null;
      const v = isThumb(k) ? thumbs.urlOf(k) : isImage(k) ? images.read(k) : mirror.get(k);
      // A key a failed migration left behind is still in localStorage.
      return v !== undefined ? v : (ls ? ls.getItem(k) : null);
    },
    setItem(k, v) {
      if (isThumb(k)) {
        // Only a new picture is stored; the object URL a read handed out is already this key's.
        const record = isDataUrl(v) ? thumbs.put(k, v) : null;
        if (record) track(store.set(k, record)).catch(writeFailed);
        return;
      }
      if (isImage(k)) return persistImage(k, images.put(k, String(v)));
      if (!isPayload(k)) {
        if (ls) ls.setItem(k, v);
        return;
      }
      const s = String(v);
      mirror.set(k, s);
      track(store.set(k, s)).catch(writeFailed);
    },
    removeItem(k) {
      if (!isOwn(k)) {
        if (ls) ls.removeItem(k);
        return;
      }
      forget(k);
      track(store.remove(k)).catch(() => { /* registry entry is the source of truth */ });
      try { ls?.removeItem(k); } catch { /* no leftover to clean */ }
    },
    keys() {
      const out = new Set([...mirror.keys(), ...images.keys(), ...thumbs.keys()]);
      for (const k of lsKeys()) out.add(k);
      return Array.from(out);
    },

    // Re-read from IndexedDB after another tab wrote: one project's keys when an id is given (one
    // read in flight per id, however many listeners ask), the whole mirror otherwise.
    refresh(id) {
      if (id == null) return refreshAll();
      if (!refreshing.has(id)) refreshing.set(id, refreshOne(id).finally(() => refreshing.delete(id)));
      return refreshing.get(id);
    },

    // An image key's data URL, its object URL's bytes read back; `keep` holds it while it is open.
    materialize: (k, keep = true) => (isImage(k) ? images.materialize(k, keep) : Promise.resolve(null))
      .then((v) => v ?? backend.getItem(k)),

    flush: () => Promise.allSettled(Array.from(pending)).then(() => {}),
  };
  // An older build's image strings become Blobs in the background; this boot reads them as they are.
  for (const [k, v] of stored) {
    if (isImage(k) && isDataUrl(v) && images.read(k) === v) { images.drop(k); persistImage(k, images.put(k, v)); }
  }
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
