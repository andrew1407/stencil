// ── ProjectsStore: pure, DOM-free project registry over a storage backend ──
// The store owns the multi-project schema in (local)Storage: a registry array
// of lightweight metadata plus one payload key per project. It is intentionally
// free of any document/window/Image usage so it can be unit-tested under Node
// with a Map-backed shim. All JSON.parse is guarded with safe defaults.
//
// KEYS
//   stencil_projects_v1        registry array of metadata entries
//   stencil_project_<id>       per-project payload { image, layout }
//   stencil_schema_migrated    "1" idempotency flag for the legacy migration
//
// Global keys drawingApp_theme / drawingApp_hotkeys are NEVER touched here.

export const REGISTRY_KEY = 'stencil_projects_v1';
export const PROJECT_PREFIX = 'stencil_project_';
export const MIGRATED_FLAG = 'stencil_schema_migrated';
export const EXPIRY_MS = 7 * 24 * 60 * 60 * 1000; // one week
export const WARN_MS = 24 * 60 * 60 * 1000; // warn once a project is within a day of expiry

// Legacy single-project keys (pre-multi-project). Kept for idempotent migration.
const LEGACY_IMAGE_KEY = 'drawingApp_image';
const LEGACY_LAYOUT_KEY = 'drawingApp_layout';

// Pure helper (also unit-tested for temp-mode): persist only when there is an
// active, non-temporary project to write to.
export const shouldPersist = (activeId, temporary) => !temporary && activeId != null;

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
    try { return Object.keys(s); } catch { return []; }
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

  #readRegistry() {
    const arr = this.#readJSON(REGISTRY_KEY, []);
    return Array.isArray(arr) ? arr : [];
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
    try { this.#storage.removeItem(this.#payloadKey(id)); } catch {}
  }

  // Wipe every project (all stencil_project_* keys + the registry). MUST NOT
  // touch drawingApp_theme / drawingApp_hotkeys or any other global key.
  clearAll() {
    for (const key of this.#keys()) {
      if (key.startsWith(PROJECT_PREFIX)) {
        try { this.#storage.removeItem(key); } catch {}
      }
    }
    try { this.#storage.removeItem(REGISTRY_KEY); } catch {}
  }

  // ── expiry ────────────────────────────────────────────────────

  isExpired(meta, now = Date.now()) {
    if (!meta || meta.updatedAt == null) return false;
    return (now - meta.updatedAt) > EXPIRY_MS;
  }

  expiresAt(meta) {
    if (!meta || meta.updatedAt == null) return null;
    return meta.updatedAt + EXPIRY_MS;
  }

  // True when a project is not yet expired but falls due within WARN_MS — the
  // cue for a warning colour in the UI. Already-expired projects return false
  // (they get the stronger "expired" treatment instead).
  isExpiringSoon(meta, now = Date.now()) {
    const at = this.expiresAt(meta);
    if (at == null) return false;
    return at > now && (at - now) <= WARN_MS;
  }

  // Prolong a project's life: restamp updatedAt = now so its 7-day expiry window
  // restarts from this moment. Alias of touch(), named for intent at call sites.
  renew(id, now = Date.now()) {
    return this.touch(id, now);
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

  // Idempotently fold the pre-multi-project keys into one project. Guarded by
  // MIGRATED_FLAG so repeated calls are no-ops. Legacy keys are NOT deleted.
  // Returns the new project id, or null when there was nothing to migrate.
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
      thumbnail: null,
      createdAt: now,
      updatedAt: now,
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
