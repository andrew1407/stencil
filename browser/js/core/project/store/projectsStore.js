// DOM-free project registry over a storage backend. Keys: registry (stencil_projects_v1), per-project
// payload (stencil_project_<id>), image (stencil_image_<id>) and thumbnail (stencil_thumb_<id>),
// migration flag (stencil_schema_migrated). Never touches the global drawingApp_theme/_hotkeys keys.
import * as naming from '../meta/projectNaming.js';
import * as periods from '../meta/projectPeriods.js';
import * as io from './projectRegistryIo.js';
import * as thumbs from './projectThumbs.js';
import * as images from './projectImages.js';
import * as sources from './projectSources.js';
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
  // The registry parsed from the string last read: an unchanged string, whoever wrote it, skips
  // the parse. Keyed on the string, not the `storage` event: core is DOM-free.
  #registry = null;

  // `storage` is localStorage-like: { getItem, setItem, removeItem }; keys() (the test shim)
  // is preferred over Object.keys for enumeration.
  constructor(storage = (typeof localStorage !== 'undefined' ? localStorage : null)) {
    this.#storage = storage;
  }

  #readJSON(key, fallback) { return io.readJSON(this.#storage, key, fallback); }

  #writeJSON(key, value) { io.writeJSON(this.#storage, key, value); }

  #payloadKey(id) { return PROJECT_PREFIX + id; }

  #readRegistry() {
    let raw = null;
    try { raw = this.#storage.getItem(REGISTRY_KEY); } catch { /* unreadable reads as empty */ }
    if (!this.#registry || this.#registry.raw !== raw) {
      const parsed = io.parseJSON(raw, []);
      const arr = Array.isArray(parsed) ? parsed : [];
      raw = this.#moveInline(arr, raw);
      for (const m of arr) io.normalizeMeta(m);
      this.#registry = { raw, arr };
    }
    return io.cloneJson(this.#registry.arr);
  }

  // Rewrites the registry as read, less the thumbnails and data-URL sources moved out; a failed
  // write leaves the stored rows inline and the next parse tries again.
  #moveInline(arr, raw) {
    const thumbed = thumbs.moveInlineThumbs(this.#storage, arr);
    if (!sources.moveInlineSources(this.#storage, arr, (id) => this.#payloadKey(id)) && !thumbed) return raw;
    const next = JSON.stringify(arr);
    try { this.#storage.setItem(REGISTRY_KEY, next); return next; } catch { return raw; }
  }

  // What was just written is the cache under its own string: the save that wrote it never re-parses.
  #writeRegistry(arr) {
    const raw = JSON.stringify(arr);
    this.#storage.setItem(REGISTRY_KEY, raw);
    this.#registry = { raw, arr: io.registryRows(arr) };
  }

  // Most-recently-updated first. [] on any error.
  list() {
    const arr = this.#readRegistry();
    return arr
      .filter(m => m && m.id != null)
      .map(m => thumbs.withThumb(this.#storage, m))
      .sort((a, b) => (b.updatedAt || 0) - (a.updatedAt || 0));
  }

  getMeta(id) {
    return thumbs.withThumb(this.#storage, this.#readRegistry().find(m => m && m.id === id) || null);
  }

  get(id) {
    const meta = this.getMeta(id);
    if (!meta) return null;
    const payload = this.#readJSON(this.#payloadKey(id), null);
    if (payload == null) return null;
    return { meta, payload: images.withImage(this.#storage, id, payload) };
  }

  resolveImage(id, image, keep = true) { return images.resolveImage(this.#storage, id, image, keep); }

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
    return thumbs.withThumb(this.#storage, arr[i]);
  }

  rename(id, name) { return this.#patchMeta(id, 'name', name); }

  // "" (theme fallback) or a normalised "#rrggbb"; DrawingApp validates first.
  setColor(id, color) { return this.#patchMeta(id, 'color', color); }

  setKeywords(id, keywords) { return this.#patchMeta(id, 'keywords', naming.normalizeKeywords(keywords)); }

  setDescription(id, description) {
    return this.#patchMeta(id, 'description', String(description == null ? '' : description).trim());
  }

  setBlankColor(id, color) { return this.#patchMeta(id, 'blankColor', color); }

  // The idle-time thumbnail lands after the save that scheduled it (thumbnail.js); the registry is untouched.
  setThumbnail(id, dataUrl) {
    const meta = this.getMeta(id);
    if (!meta) return null;
    thumbs.writeThumb(this.#storage, id, dataUrl);
    meta.thumbnail = dataUrl;
    return meta;
  }

  findByImage(source, name) { return naming.findByImage(this.list(), source, name); }

  copyName(baseName, source) { return naming.copyName(this.list(), baseName, source); }

  defaultName() { return naming.defaultName(this.#readRegistry()); }

  // Bumps updatedAt. QuotaExceededError from the backend propagates.
  upsert(meta, payload) {
    const now = Date.now();
    const stored = sources.withSourceRef({ ...meta, updatedAt: now });
    if (stored.createdAt == null) stored.createdAt = now;

    // Image, payload and thumbnail first, so a quota failure leaves the registry untouched; the
    // image is written only when it changed and never re-serialised with the layout.
    images.writeImage(this.#storage, stored.id, payload?.image);
    this.#writeJSON(this.#payloadKey(stored.id), images.withoutImage(payload));
    thumbs.writeThumb(this.#storage, stored.id, stored.thumbnail);

    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === stored.id);
    if (i === -1) arr.push(thumbs.rowOf(stored));
    else arr[i] = thumbs.rowOf(stored);
    this.#writeRegistry(arr);
    return stored;
  }

  touch(id, now = Date.now()) {
    const arr = this.#readRegistry();
    const i = arr.findIndex(m => m && m.id === id);
    if (i === -1) return null;
    arr[i].updatedAt = now;
    this.#writeRegistry(arr);
    return thumbs.withThumb(this.#storage, arr[i]);
  }

  remove(id) {
    const arr = this.#readRegistry().filter(m => !(m && m.id === id));
    this.#writeRegistry(arr);
    try {
      this.#storage.removeItem(this.#payloadKey(id));
    } catch {
      /* registry entry is the source of truth */
    }
    images.removeImage(this.#storage, id);
    thumbs.removeThumb(this.#storage, id);
  }

  clearAll() {
    io.clearProjectKeys(this.#storage, REGISTRY_KEY, PROJECT_PREFIX);
    images.clearImages(this.#storage);
    thumbs.clearThumbs(this.#storage);
  }

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
    return thumbs.withThumb(this.#storage, arr[i]);
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
