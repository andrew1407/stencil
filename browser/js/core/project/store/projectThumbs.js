// Project thumbnails, one key per project beside the registry: a save rewrites the small registry,
// never every project's 480px JPEG. Through projectsBackend.js the key is an IndexedDB Blob read back
// as an object URL (thumbBlobs.js); a row keeps `thumbnail: null` where its data moved out.
import { storageKeys } from './projectRegistryIo.js';

export const THUMB_PREFIX = 'stencil_thumb_';
const keyOf = (id) => THUMB_PREFIX + id;

export const readThumb = (storage, id) => {
  try { return storage.getItem(keyOf(id)); } catch { return null; }
};

// The meta as callers have always read it: its key's thumbnail on the row.
export const withThumb = (storage, m) => {
  const t = m?.id != null ? readThumb(storage, m.id) : null;
  if (t != null) m.thumbnail = t;
  return m;
};

// A string lands in the key when it changed (the backend skips a picture it already holds);
// anything else clears it, as a replaced row would. QuotaExceededError propagates.
export const writeThumb = (storage, id, thumbnail) => {
  const cur = readThumb(storage, id);
  if (typeof thumbnail === 'string') { if (cur !== thumbnail) storage.setItem(keyOf(id), thumbnail); }
  else if (cur != null) storage.removeItem(keyOf(id));
};

// The row the registry stores for a meta.
export const rowOf = (meta) => (typeof meta.thumbnail === 'string' ? { ...meta, thumbnail: null } : meta);

// An inline thumbnail (an older build wrote it) moves to its key; true when a row changed. A key
// that cannot be written leaves its row inline, so nothing is lost.
export const moveInlineThumbs = (storage, rows) => {
  let moved = false;
  for (const m of rows) {
    if (!m || m.id == null || typeof m.thumbnail !== 'string') continue;
    try { storage.setItem(keyOf(m.id), m.thumbnail); } catch { continue; }
    m.thumbnail = null;
    moved = true;
  }
  return moved;
};

export const removeThumb = (storage, id) => {
  try { storage.removeItem(keyOf(id)); } catch { /* the registry row is the source of truth */ }
};

export const clearThumbs = (storage) => {
  for (const k of storageKeys(storage)) {
    if (!k.startsWith(THUMB_PREFIX)) continue;
    try { storage.removeItem(k); } catch { /* keep wiping the rest */ }
  }
};
