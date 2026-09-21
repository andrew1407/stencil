// DOM-free project registry over a storage backend. Keys: registry (stencil_projects_v1),
// per-project payload (stencil_project_<id>), migration flag (stencil_schema_migrated).
// Never touches the global drawingApp_theme/_hotkeys keys.
import * as naming from '../meta/projectNaming.js';
import * as periods from '../meta/projectPeriods.js';
import * as io from './projectRegistryIo.js';
import { migrateLegacyProject } from './projectsMigrate.js';

export const REGISTRY_KEY = 'stencil_projects_v1';
export const PROJECT_PREFIX = 'stencil_project_';
export const MIGRATED_FLAG = 'stencil_schema_migrated';

export { EXPIRY_MS, WARN_MS, DEFAULT_PERIOD, PERIOD_MS, PERIOD_ORDER, periodMs, addPeriod } from '../meta/projectPeriods.js';
export { normalizeKeywords, baseProjectName } from '../meta/projectNaming.js';

// Persist only when there is an active, non-temporary project to write to.
export const shouldPersist = (activeId, temporary) => !temporary && activeId != null;

export class ProjectsStore {
  #storage;

  // `storage` is localStorage-like: { getItem, setItem, removeItem }; keys() (the test shim)
  // is preferred over Object.keys for enumeration.
  constructor(storage = (typeof localStorage !== 'undefined' ? localStorage : null)) {
    this.#storage = storage;
  }

  #readJSON(key, fallback) { return io.readJSON(this.#storage, key, fallback); }

  #writeJSON(key, value) { io.writeJSON(this.#storage, key, value); }

  #payloadKey(id) { return PROJECT_PREFIX + id; }

  #readRegistry() {
    const arr = this.#readJSON(REGISTRY_KEY, []);
    if (!Array.isArray(arr)) return [];
    for (const m of arr) io.normalizeMeta(m);
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

  nameExists(name, exceptId = null) { return naming.nameExists(this.#readRegistry(), name, exceptId); }

  validateName(name, exceptId = null) { return naming.validateName(this.#readRegistry(), name, exceptId); }

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

  setKeywords(id, keywords) { return this.#patchMeta(id, 'keywords', naming.normalizeKeywords(keywords)); }

  setDescription(id, description) {
    return this.#patchMeta(id, 'description', String(description == null ? '' : description).trim());
  }

  setBlankColor(id, color) { return this.#patchMeta(id, 'blankColor', color); }

  // The idle-time thumbnail lands after the save that scheduled it (thumbnail.js).
  setThumbnail(id, dataUrl) { return this.#patchMeta(id, 'thumbnail', dataUrl); }

  findByImage(source, name) { return naming.findByImage(this.list(), source, name); }

  copyName(baseName, source) { return naming.copyName(this.list(), baseName, source); }

  defaultName() { return naming.defaultName(this.#readRegistry()); }


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

  clearAll() { io.clearProjectKeys(this.#storage, REGISTRY_KEY, PROJECT_PREFIX); }


  isExpired(meta, now = Date.now()) { return periods.isExpired(meta, now); }

  expiresAt(meta) { return periods.expiresAt(meta); }

  isExpiringSoon(meta, now = Date.now()) { return periods.isExpiringSoon(meta, now); }

  // expiresAt = now + its refresh period (the Refresh button and the open-time snap);
  // turns off keep-forever. Unknown id → null.
  renew(id, now = Date.now()) {
    const m = this.getMeta(id);
    if (!m) return null;
    const period = m.refreshPeriod || periods.DEFAULT_PERIOD;
    return this.setExpiration(id, { expiresAt: periods.addPeriod(now, period), refreshPeriod: period });
  }

  // No updatedAt bump; expiresAt of 0 means "keep forever"; only the provided keys are written.
  setExpiration(id, { expiresAt, refreshPeriod, autoRefresh } = {}) {
    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === id);
    if (i === -1) return null;
    if (expiresAt != null) arr[i].expiresAt = expiresAt;
    if (refreshPeriod != null) arr[i].refreshPeriod = refreshPeriod || periods.DEFAULT_PERIOD;
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


  migrateLegacy(now = Date.now()) {
    return migrateLegacyProject(this, this.#storage, MIGRATED_FLAG, now);
  }
}
