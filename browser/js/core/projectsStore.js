// DOM-free project registry over a storage backend. Keys: registry (stencil_projects_v1),
// per-project payload (stencil_project_<id>), migration flag (stencil_schema_migrated).
// Never touches the global drawingApp_theme/_hotkeys keys.

export const REGISTRY_KEY = 'stencil_projects_v1';
export const PROJECT_PREFIX = 'stencil_project_';
export const MIGRATED_FLAG = 'stencil_schema_migrated';
export const EXPIRY_MS = 7 * 24 * 60 * 60 * 1000; // one week (also the "week" preset)
export const WARN_MS = 24 * 60 * 60 * 1000; // warn once a project is within a day of expiry

// Fixed durations (month=30d, year=365d) so this and core/state/projectsStore.cpp
// (ProjectsStore::periodMs) stay identical with no calendar library; PERIOD_ORDER drives
// the modal's selector.
const DAY_MS = 24 * 60 * 60 * 1000;
export const DEFAULT_PERIOD = 'week';
export const PERIOD_MS = {
  day: DAY_MS,
  week: 7 * DAY_MS,
  fortnight: 14 * DAY_MS,
  month: 30 * DAY_MS,
  '3month': 90 * DAY_MS,
  '6month': 180 * DAY_MS,
  year: 365 * DAY_MS,
};
export const PERIOD_ORDER = ['day', 'week', 'fortnight', 'month', '3month', '6month', 'year'];

// Trim, drop blanks, dedupe case-insensitively (first-seen order). Matches the server's
// joinKeywords so a keyword set round-trips identically.
export const normalizeKeywords = (keywords) => {
  const out = [];
  const seen = new Set();
  for (const raw of (Array.isArray(keywords) ? keywords : [])) {
    const k = String(raw == null ? '' : raw).trim();
    if (!k) continue;
    const lk = k.toLowerCase();
    if (seen.has(lk)) continue;
    seen.add(lk);
    out.push(k);
  }
  return out;
};
// Unknown/empty → one week. Mirrors core periodMs.
export const periodMs = (period) => PERIOD_MS[period] ?? EXPIRY_MS;
// Mirrors core addPeriod.
export const addPeriod = (from, period) => from + periodMs(period);

// Legacy single-project keys, kept for idempotent migration.
const LEGACY_IMAGE_KEY = 'drawingApp_image';
const LEGACY_LAYOUT_KEY = 'drawingApp_layout';

// Persist only when there is an active, non-temporary project to write to.
export const shouldPersist = (activeId, temporary) => !temporary && activeId != null;

// Strip a trailing " (N)" so "photo (2)" and "photo" group together.
export const baseProjectName = (name) =>
  String(name || '').replace(/\s*\(\d+\)\s*$/, '').trim();

export class ProjectsStore {
  #storage;

  // `storage` is localStorage-like: { getItem, setItem, removeItem }; keys() (the test shim)
  // is preferred over Object.keys for enumeration.
  constructor(storage = (typeof localStorage !== 'undefined' ? localStorage : null)) {
    this.#storage = storage;
  }

  #keys() {
    const s = this.#storage;
    if (!s) return [];
    if (typeof s.keys === 'function') return Array.from(s.keys());
    try {
      return Object.keys(s);
    } catch {
      return [];
    }
  }

  #readJSON(key, fallback) {
    try {
      const raw = this.#storage.getItem(key);
      if (raw == null) return fallback;
      const val = JSON.parse(raw);
      return val == null ? fallback : val;
    } catch {
      return fallback;
    }
  }

  #writeJSON(key, value) {
    // QuotaExceededError propagates so the DOM adapter can evict + retry.
    this.#storage.setItem(key, JSON.stringify(value));
  }

  #payloadKey(id) { return PROJECT_PREFIX + id; }

  // Only ABSENT fields are filled, so an explicit expiresAt of 0 (keep forever) survives;
  // legacy projects get expiresAt = updatedAt + one week.
  #normalizeMeta(m) {
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
  }

  #readRegistry() {
    const arr = this.#readJSON(REGISTRY_KEY, []);
    if (!Array.isArray(arr)) return [];
    for (const m of arr) this.#normalizeMeta(m);
    return arr;
  }

  #writeRegistry(arr) {
    this.#writeJSON(REGISTRY_KEY, arr);
  }


  // Most-recently-updated first. [] on any error.
  list() {
    const arr = this.#readRegistry();
    return arr
      .filter(m => m && m.id != null)
      .sort((a, b) => (b.updatedAt || 0) - (a.updatedAt || 0));
  }

  getMeta(id) {
    return this.#readRegistry().find(m => m && m.id === id) || null;
  }

  get(id) {
    const meta = this.getMeta(id);
    if (!meta) return null;
    const payload = this.#readJSON(this.#payloadKey(id), null);
    if (payload == null) return null;
    return { meta, payload };
  }


  createId() {
    const rnd = () => Math.random().toString(36).slice(2, 8);
    let id;
    do {
      id = 'p_' + Date.now().toString(36) + '_' + rnd();
    } while (this.getMeta(id));
    return id;
  }

  // Trimmed, case-insensitive; drives the "no duplicate names" guard in renameProject.
  nameExists(name, exceptId = null) {
    const n = String(name || '').trim().toLowerCase();
    if (!n) return false;
    return this.#readRegistry().some(m =>
      m && m.id !== exceptId && String(m.name || '').trim().toLowerCase() === n);
  }

  // → { ok, reason }; gates the rename ✓ button. `exceptId` is the project being renamed.
  validateName(name, exceptId = null) {
    const clean = String(name || '').trim();
    if (!clean) return { ok: false, reason: 'Name can’t be empty' };
    if (clean.length > 80) return { ok: false, reason: 'Name is too long (max 80 characters)' };
    if (this.nameExists(clean, exceptId)) return { ok: false, reason: `“${clean}” is already taken` };
    return { ok: true, reason: '' };
  }

  // rename and the set* methods: null on an unknown id; payload + updatedAt untouched.
  #patchMeta(id, field, value) {
    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === id);
    if (i === -1) return null;
    arr[i][field] = value;
    this.#writeRegistry(arr);
    return arr[i];
  }

  rename(id, name) { return this.#patchMeta(id, 'name', name); }

  // "" (theme fallback) or a normalised "#rrggbb"; DrawingApp validates first.
  setColor(id, color) { return this.#patchMeta(id, 'color', color); }

  setKeywords(id, keywords) { return this.#patchMeta(id, 'keywords', normalizeKeywords(keywords)); }

  setDescription(id, description) {
    return this.#patchMeta(id, 'description', String(description == null ? '' : description).trim());
  }

  setBlankColor(id, color) { return this.#patchMeta(id, 'blankColor', color); }

  // The idle-time thumbnail lands after the save that scheduled it (thumbnail.js).
  setThumbnail(id, dataUrl) { return this.#patchMeta(id, 'thumbnail', dataUrl); }

  // Same image = identical non-empty `source` URL, else a base-name match. Drives the
  // extension-launch "resume" path + copy-numbering.
  findByImage(source, name) {
    const src = source || '';
    const base = baseProjectName(name || '');
    return this.list().filter(m => {
      if (src) return (m.source || '') === src;
      return !!base && baseProjectName(m.name || '') === base;
    });
  }

  // Next free "Name (N)" for a copy: the bare base name when free, else the lowest unused N ≥ 1.
  copyName(baseName, source) {
    const base = baseProjectName(baseName || '') || (baseName || 'Untitled');
    const taken = new Set(this.findByImage(source, base).map(m => m.name || ''));
    if (!taken.has(base)) return base;
    let n = 1;
    while (taken.has(`${base} (${n})`)) n++;
    return `${base} (${n})`;
  }

  defaultName() {
    let max = 0;
    for (const m of this.#readRegistry()) {
      const match = /^Untitled (\d+)$/.exec((m && m.name) || '');
      if (match) max = Math.max(max, parseInt(match[1], 10));
    }
    return `Untitled ${max + 1}`;
  }


  // Bumps updatedAt. QuotaExceededError from the backend propagates.
  upsert(meta, payload) {
    const now = Date.now();
    const stored = { ...meta, updatedAt: now };
    if (stored.createdAt == null) stored.createdAt = now;

    // Payload first, so a quota failure leaves the registry untouched.
    this.#writeJSON(this.#payloadKey(stored.id), payload);

    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === stored.id);
    if (i === -1) arr.push(stored);
    else arr[i] = stored;
    this.#writeRegistry(arr);
    return stored;
  }

  touch(id, now = Date.now()) {
    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === id);
    if (i === -1) return null;
    arr[i].updatedAt = now;
    this.#writeRegistry(arr);
    return arr[i];
  }

  remove(id) {
    const arr = this.#readRegistry().filter(m => !(m && m.id === id));
    this.#writeRegistry(arr);
    try {
      this.#storage.removeItem(this.#payloadKey(id));
    } catch {
      /* registry entry is the source of truth */
    }
  }

  // MUST NOT touch drawingApp_theme / drawingApp_hotkeys or any other global key.
  clearAll() {
    for (const key of this.#keys()) {
      if (key.startsWith(PROJECT_PREFIX)) {
        try {
          this.#storage.removeItem(key);
        } catch {
          /* keep wiping the rest */
        }
      }
    }
    try {
      this.#storage.removeItem(REGISTRY_KEY);
    } catch {
      /* nothing left to wipe */
    }
  }


  // expiresAt of 0 (or absent) == keep forever.
  isExpired(meta, now = Date.now()) {
    if (!meta || !meta.expiresAt) return false;
    return now > meta.expiresAt;
  }

  expiresAt(meta) {
    if (!meta || !meta.expiresAt) return null;
    return meta.expiresAt;
  }

  // Not yet expired but due within WARN_MS; already-expired projects return false.
  isExpiringSoon(meta, now = Date.now()) {
    const at = this.expiresAt(meta);
    if (at == null) return false;
    return at > now && (at - now) <= WARN_MS;
  }

  // expiresAt = now + its refresh period (the Refresh button and the open-time snap);
  // turns off keep-forever. Unknown id → null.
  renew(id, now = Date.now()) {
    const m = this.getMeta(id);
    if (!m) return null;
    const period = m.refreshPeriod || DEFAULT_PERIOD;
    return this.setExpiration(id, { expiresAt: addPeriod(now, period), refreshPeriod: period });
  }

  // No updatedAt bump; expiresAt of 0 means "keep forever"; only the provided keys are written.
  setExpiration(id, { expiresAt, refreshPeriod, autoRefresh } = {}) {
    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === id);
    if (i === -1) return null;
    if (expiresAt != null) arr[i].expiresAt = expiresAt;
    if (refreshPeriod != null) arr[i].refreshPeriod = refreshPeriod || DEFAULT_PERIOD;
    if (autoRefresh != null) arr[i].autoRefresh = !!autoRefresh;
    this.#writeRegistry(arr);
    return arr[i];
  }

  sweepExpired(now = Date.now()) {
    const removed = [];
    for (const m of this.#readRegistry()) {
      if (this.isExpired(m, now)) {
        this.remove(m.id);
        removed.push(m.id);
      }
    }
    return removed;
  }


  // Guarded by MIGRATED_FLAG (repeated calls no-op); legacy keys are NOT deleted. Returns the
  // new project id, or null when there was nothing to migrate.
  migrateLegacy(now = Date.now()) {
    if (this.#storage.getItem(MIGRATED_FLAG)) return null;

    const image = this.#storage.getItem(LEGACY_IMAGE_KEY) || null;
    const layout = this.#readJSON(LEGACY_LAYOUT_KEY, null);

    if (image == null && layout == null) {
      this.#storage.setItem(MIGRATED_FLAG, '1');
      return null;
    }

    const safeLayout = (layout && typeof layout === 'object') ? layout : {};
    const id = this.createId();
    const meta = {
      id,
      name: safeLayout.imageBaseName || 'Untitled 1',
      color: '',
      description: '',
      thumbnail: null,
      createdAt: now,
      updatedAt: now,
      expiresAt: addPeriod(now, DEFAULT_PERIOD),
      refreshPeriod: DEFAULT_PERIOD,
      autoRefresh: true,
      hasImage: !!image,
      imageW: safeLayout.imageWidth || null,
      imageH: safeLayout.imageHeight || null,
      // Filled on the first real save once page metrics are live.
      lineLengthCm: 0,
    };
    this.upsert(meta, { image, layout: safeLayout });
    // Pin to the requested `now` so the migrated timestamps are deterministic.
    this.touch(id, now);
    this.#storage.setItem(MIGRATED_FLAG, '1');
    return id;
  }
}
