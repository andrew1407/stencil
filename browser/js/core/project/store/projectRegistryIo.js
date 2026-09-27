// The localStorage-shaped plumbing under ProjectsStore: key enumeration, JSON read/write and
// the registry meta default-fill every read passes through.
import { EXPIRY_MS, DEFAULT_PERIOD } from '../meta/projectPeriods.js';

// `keys()` (the test shim) is preferred over Object.keys for enumeration.
export const storageKeys = (storage) => {
  if (!storage) return [];
  if (typeof storage.keys === 'function') return Array.from(storage.keys());
  try {
    return Object.keys(storage);
  } catch {
    return [];
  }
};

export const parseJSON = (raw, fallback) => {
  if (raw == null) return fallback;
  try {
    const val = JSON.parse(raw);
    return val == null ? fallback : val;
  } catch {
    return fallback;
  }
};

export const readJSON = (storage, key, fallback) => {
  let raw;
  try { raw = storage.getItem(key); } catch { return fallback; }
  return parseJSON(raw, fallback);
};

// QuotaExceededError propagates so the DOM adapter can evict + retry.
export const writeJSON = (storage, key, value) => {
  storage.setItem(key, JSON.stringify(value));
};

// Only ABSENT fields are filled, so an explicit expiresAt of 0 (keep forever) survives;
// legacy projects get expiresAt = updatedAt + one week.
export const normalizeMeta = (m) => {
  if (!m || typeof m !== 'object') return m;
  if (m.expiresAt == null) m.expiresAt = (m.updatedAt || 0) + EXPIRY_MS;
  if (m.refreshPeriod == null) m.refreshPeriod = DEFAULT_PERIOD;
  if (m.autoRefresh == null) m.autoRefresh = true;
  if (!Array.isArray(m.keywords)) m.keywords = [];
  if (typeof m.description !== 'string') m.description = '';
  if (typeof m.lineLengthCm !== 'number') m.lineLengthCm = 0;
  if (typeof m.blank !== 'boolean') m.blank = false;
  if (typeof m.blankColor !== 'string') m.blankColor = '';
  return m;
};

// What a JSON round trip of `v` yields, without one: an undefined field goes, an undefined or
// non-finite element is null. Strings are immutable, so only the containers copy.
export const cloneJson = (v) => {
  if (Array.isArray(v)) return v.map((x) => (x === undefined ? null : cloneJson(x)));
  if (v && typeof v === 'object') {
    const out = {};
    for (const [k, x] of Object.entries(v)) if (x !== undefined) out[k] = cloneJson(x);
    return out;
  }
  return typeof v === 'number' && !Number.isFinite(v) ? null : v;
};

// The rows a read of the registry string `JSON.stringify(arr)` parses to.
export const registryRows = (arr) => {
  const rows = cloneJson(arr);
  for (const m of rows) normalizeMeta(m);
  return rows;
};

// MUST NOT touch drawingApp_theme / drawingApp_hotkeys or any other global key.
export const clearProjectKeys = (storage, registryKey, prefix) => {
  for (const key of storageKeys(storage)) {
    if (key.startsWith(prefix)) {
      try {
        storage.removeItem(key);
      } catch {
        /* keep wiping the rest */
      }
    }
  }
  try {
    storage.removeItem(registryKey);
  } catch {
    /* nothing left to wipe */
  }
};
