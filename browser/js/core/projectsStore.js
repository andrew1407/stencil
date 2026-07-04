// ── ProjectsStore: pure, DOM-free project registry over a storage backend ──
// Owns the multi-project schema in (local)Storage: a registry array of metadata plus one
// payload key per project. DOM-free (unit-tests under Node with a Map-backed shim); all
// JSON.parse guarded with safe defaults. Keys: registry (stencil_projects_v1), per-project
// payload (stencil_project_<id>), migration flag (stencil_schema_migrated). Never touches
// global drawingApp_theme/_hotkeys keys.

export const REGISTRY_KEY = 'stencil_projects_v1';
export const PROJECT_PREFIX = 'stencil_project_';
export const MIGRATED_FLAG = 'stencil_schema_migrated';
export const EXPIRY_MS = 7 * 24 * 60 * 60 * 1000; // one week (also the "week" preset)
export const WARN_MS = 24 * 60 * 60 * 1000; // warn once a project is within a day of expiry

// Refresh presets. Fixed durations (month=30d, year=365d, …) so this JS port and
// core/state/projectsStore.cpp stay identical with no calendar library; the
// custom-calendar pick sets an exact date instead. PERIOD_ORDER drives the modal's
// selector. Keep in sync with ProjectsStore::periodMs in the C++ core.
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
// Milliseconds for a preset; unknown/empty → one week. Mirrors core periodMs.
export const periodMs = (period) => PERIOD_MS[period] ?? EXPIRY_MS;
// from + periodMs(period). Mirrors core addPeriod.
export const addPeriod = (from, period) => from + periodMs(period);

// Legacy single-project keys (pre-multi-project). Kept for idempotent migration.
const LEGACY_IMAGE_KEY = 'drawingApp_image';
const LEGACY_LAYOUT_KEY = 'drawingApp_layout';

// Pure helper (also unit-tested for temp-mode): persist only when there is an
// active, non-temporary project to write to.
export const shouldPersist = (activeId, temporary) => !temporary && activeId != null;

// Strip a trailing copy suffix " (N)" so "photo (2)" and "photo" group together
// when matching/numbering copies. Pure, unit-tested.
export const baseProjectName = (name) =>
  String(name || '').replace(/\s*\(\d+\)\s*$/, '').trim();

export class ProjectsStore {
  #storage;

  // `storage` is a localStorage-like object: { getItem, setItem, removeItem }.
  // For key enumeration we prefer storage.keys (the test shim) and fall back to
  // Object.keys for the real localStorage. Default backend is localStorage.
  constructor(storage = (typeof localStorage !== 'undefined' ? localStorage : null)) {
    this.#storage = storage;
  }

  // ── internal helpers ──────────────────────────────────────────

  // Enumerate all keys held by the backend, regardless of back-end shape.
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
    // Let QuotaExceededError propagate so the DOM adapter can evict + retry.
    this.#storage.setItem(key, JSON.stringify(value));
  }

  #payloadKey(id) { return PROJECT_PREFIX + id; }

  // Default-fill the expiration fields for projects saved before this schema.
  // Only fills ABSENT fields, so an explicit expiresAt of 0 (keep forever) is
  // preserved. Legacy projects get expiresAt = updatedAt + one week, matching
  // the old derived rule so behaviour doesn't jump on upgrade.
  #normalizeMeta(m) {
    if (!m || typeof m !== 'object') return m;
    if (m.expiresAt == null) m.expiresAt = (m.updatedAt || 0) + EXPIRY_MS;
    if (m.refreshPeriod == null) m.refreshPeriod = DEFAULT_PERIOD;
    if (m.autoRefresh == null) m.autoRefresh = true;
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

  // ── registry / project reads ──────────────────────────────────

  // All projects, most-recently-updated first. [] on any error.
  list() {
    const arr = this.#readRegistry();
    // .filter already produces a fresh array, so sorting it in place is safe
    // (no .slice() copy needed) and never touches the stored registry.
    return arr
      .filter(m => m && m.id != null)
      .sort((a, b) => (b.updatedAt || 0) - (a.updatedAt || 0));
  }

  getMeta(id) {
    return this.#readRegistry().find(m => m && m.id === id) || null;
  }

  // Full project { meta, payload } or null if either part is missing.
  get(id) {
    const meta = this.getMeta(id);
    if (!meta) return null;
    const payload = this.#readJSON(this.#payloadKey(id), null);
    if (payload == null) return null;
    return { meta, payload };
  }

  // ── id / name allocation ──────────────────────────────────────

  createId() {
    const rnd = () => Math.random().toString(36).slice(2, 8);
    let id;
    do {
      id = 'p_' + Date.now().toString(36) + '_' + rnd();
    } while (this.getMeta(id));
    return id;
  }

  // True when another project (id ≠ exceptId) already uses `name` (trimmed,
  // case-insensitive). Pure; drives the "no duplicate names" guard in renameProject.
  nameExists(name, exceptId = null) {
    const n = String(name || '').trim().toLowerCase();
    if (!n) return false;
    return this.#readRegistry().some(m =>
      m && m.id !== exceptId && String(m.name || '').trim().toLowerCase() === n);
  }

  // Validate a proposed project name → { ok, reason }. Pure; the UI uses it to gate
  // the rename ✓ button and show why it's disabled. `exceptId` is the project being
  // renamed (so its own current name doesn't count as a duplicate).
  validateName(name, exceptId = null) {
    const clean = String(name || '').trim();
    if (!clean) return { ok: false, reason: 'Name can’t be empty' };
    if (clean.length > 80) return { ok: false, reason: 'Name is too long (max 80 characters)' };
    if (this.nameExists(clean, exceptId)) return { ok: false, reason: `“${clean}” is already taken` };
    return { ok: true, reason: '' };
  }

  // Rename a project: update its registry meta.name in place. No-op (returns null)
  // when the id is unknown. Does not touch the payload or bump updatedAt.
  rename(id, name) {
    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === id);
    if (i === -1) return null;
    arr[i].name = name;
    this.#writeRegistry(arr);
    return arr[i];
  }

  // Set a project's accent colour in its registry meta in place. `color` is "" (no
  // custom colour → theme fallback) or a normalised "#rrggbb"; the DrawingApp setter
  // validates before calling. No-op (returns null) when the id is unknown. Like
  // rename(), leaves the payload + updatedAt untouched.
  setColor(id, color) {
    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === id);
    if (i === -1) return null;
    arr[i].color = color;
    this.#writeRegistry(arr);
    return arr[i];
  }

  // Projects from the same image, most-recently-updated first. Match = identical non-empty
  // `source` URL; when `source` is empty, fall back to base-name match (so a local "photo"
  // still groups with copies). Drives the extension-launch "resume" path + copy-numbering below.
  findByImage(source, name) {
    const src = source || '';
    const base = baseProjectName(name || '');
    return this.list().filter(m => {
      if (src) return (m.source || '') === src;
      return !!base && baseProjectName(m.name || '') === base;
    });
  }

  // Next free "Name (N)" for a new copy of an image already opened as one or more
  // projects (matched by findByImage). Returns the bare base name when no project
  // with that image/name exists yet, else the lowest unused (N) ≥ 1.
  copyName(baseName, source) {
    const base = baseProjectName(baseName || '') || (baseName || 'Untitled');
    const taken = new Set(this.findByImage(source, base).map(m => m.name || ''));
    if (!taken.has(base)) return base;
    let n = 1;
    while (taken.has(`${base} (${n})`)) n++;
    return `${base} (${n})`;
  }

  // "Untitled N" where N is one past the highest existing Untitled index.
  defaultName() {
    let max = 0;
    for (const m of this.#readRegistry()) {
      const match = /^Untitled (\d+)$/.exec((m && m.name) || '');
      if (match) max = Math.max(max, parseInt(match[1], 10));
    }
    return `Untitled ${max + 1}`;
  }

  // ── writes ────────────────────────────────────────────────────

  // Write/replace a registry entry + its payload. Bumps updatedAt. Returns the
  // stored meta. QuotaExceededError from the backend propagates to the caller.
  upsert(meta, payload) {
    const now = Date.now();
    const stored = { ...meta, updatedAt: now };
    if (stored.createdAt == null) stored.createdAt = now;

    // Write the payload first so a quota failure leaves the registry untouched.
    this.#writeJSON(this.#payloadKey(stored.id), payload);

    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === stored.id);
    if (i === -1) arr.push(stored);
    else arr[i] = stored;
    this.#writeRegistry(arr);
    return stored;
  }

  // Bump a project's updatedAt without rewriting its payload.
  touch(id, now = Date.now()) {
    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === id);
    if (i === -1) return null;
    // arr is a fresh parse from #readRegistry (nothing else holds arr[i]), so
    // bump the field in place instead of cloning the whole meta object.
    arr[i].updatedAt = now;
    this.#writeRegistry(arr);
    return arr[i];
  }

  // Delete one project: its registry entry + payload key. Leaves others intact.
  remove(id) {
    const arr = this.#readRegistry().filter(m => !(m && m.id === id));
    this.#writeRegistry(arr);
    try {
      this.#storage.removeItem(this.#payloadKey(id));
    } catch {
      /* key already gone or storage unavailable — registry entry is the source of truth */
    }
  }

  // Wipe every project (all stencil_project_* keys + the registry). MUST NOT
  // touch drawingApp_theme / drawingApp_hotkeys or any other global key.
  clearAll() {
    for (const key of this.#keys()) {
      if (key.startsWith(PROJECT_PREFIX)) {
        try {
          this.#storage.removeItem(key);
        } catch {
          /* key already gone or storage unavailable — skip it, keep wiping the rest */
        }
      }
    }
    try {
      this.#storage.removeItem(REGISTRY_KEY);
    } catch {
      /* registry already gone or storage unavailable — nothing left to wipe */
    }
  }

  // ── expiry ────────────────────────────────────────────────────

  // All keyed on the stored expiresAt; expiresAt of 0 (or absent) == keep forever.
  isExpired(meta, now = Date.now()) {
    if (!meta || !meta.expiresAt) return false;  // keep forever
    return now > meta.expiresAt;
  }

  expiresAt(meta) {
    if (!meta || !meta.expiresAt) return null;  // keep forever → no date
    return meta.expiresAt;
  }

  // True when a project is not yet expired but falls due within WARN_MS — the
  // cue for a warning colour in the UI. Already-expired projects return false
  // (they get the stronger "expired" treatment instead).
  isExpiringSoon(meta, now = Date.now()) {
    const at = this.expiresAt(meta);
    if (at == null) return false;
    return at > now && (at - now) <= WARN_MS;
  }

  // Prolong a project's life: set expiresAt = now + its refresh period. This is
  // the Refresh button and the open-time auto-refresh snap. Turns off keep-forever
  // if it was on (a fresh window is exactly what refresh means). No id → null.
  renew(id, now = Date.now()) {
    const m = this.getMeta(id);
    if (!m) return null;
    const period = m.refreshPeriod || DEFAULT_PERIOD;
    return this.setExpiration(id, { expiresAt: addPeriod(now, period), refreshPeriod: period });
  }

  // Set a project's expiration fields exactly (no updatedAt bump, like rename/setColor).
  // expiresAt of 0 means "keep forever". Only the provided keys are written. This is
  // what the expiration modal calls. No-op (returns null) when the id is unknown.
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

  // Remove every expired project; return the removed ids.
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

  // ── legacy migration ──────────────────────────────────────────

  // Idempotently fold the pre-multi-project keys into one project. Guarded by MIGRATED_FLAG
  // (repeated calls no-op); legacy keys are NOT deleted. Returns the new project id, or
  // null when there was nothing to migrate.
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
      thumbnail: null,
      createdAt: now,
      updatedAt: now,
      expiresAt: addPeriod(now, DEFAULT_PERIOD),
      refreshPeriod: DEFAULT_PERIOD,
      autoRefresh: true,
      hasImage: !!image,
      imageW: safeLayout.imageWidth || null,
      imageH: safeLayout.imageHeight || null,
    };
    this.upsert(meta, { image, layout: safeLayout });
    // upsert bumps updatedAt to Date.now(); pin to the requested `now` so the
    // migrated project's timestamps are deterministic for callers/tests.
    this.touch(id, now);
    this.#storage.setItem(MIGRATED_FLAG, '1');
    return id;
  }
}
