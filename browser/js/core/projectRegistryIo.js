// The localStorage-shaped plumbing under ProjectsStore: key enumeration, JSON read/write and
// the registry meta default-fill every read passes through.
import { EXPIRY_MS, DEFAULT_PERIOD } from './projectPeriods.js';

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

export const readJSON = (storage, key, fallback) => {
  try {
    const raw = storage.getItem(key);
    if (raw == null) return fallback;
    const val = JSON.parse(raw);
    return val == null ? fallback : val;
  } catch {
    return fallback;
  }
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
