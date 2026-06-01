// ── Hotkey registry singleton ───────────────────────────────────
// Owns the mutable keyboard-shortcut bindings that used to live as global
// window.HOTKEYS / window.HOTKEY_DEFAULTS / window.saveHotkeys /
// window.updateCtxHotkeyHints and were read as bare globals from settingsModal,
// drawingApp and contextMenu. The state now lives in #private fields of one
// `hotkeys` instance, imported explicitly by those consumers — no globals, no
// implicit window reads.
//
// Behavior is preserved exactly: defaults come from the same hotkeysConfig.json,
// saved overrides are merged from localStorage under the same 'drawingApp_hotkeys'
// key, save() writes the same JSON, and updateCtxHints() patches the same
// [data-hk] elements. The localStorage load/save and DOM work are guarded by
// typeof window/document/localStorage so importing this leaf module in Node
// (transitively, via any browser module) stays inert and never throws.
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };

const STORAGE_KEY = 'drawingApp_hotkeys';

class Hotkeys {
  // Frozen id → default-combo map (the reset target).
  #defaults = Object.freeze(Object.fromEntries(HOTKEY_DEFS.map(h => [h.id, h.default])));
  // Live id → combo map (mutable; starts from defaults, then merges saved).
  #current;

  constructor() {
    this.#current = { ...this.#defaults };
    // Merge persisted overrides only in the browser.
    if (typeof localStorage === 'undefined') return;
    try {
      const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}');
      for (const k in saved)
        if (k in this.#current) this.#current[k] = saved[k];
    } catch {
      /* ignore */
    }
  }

  // Current combo for an id (undefined if unknown).
  get(id) {
    return this.#current[id];
  }

  // Default combo for an id.
  getDefault(id) {
    return this.#defaults[id];
  }

  // Set/override a binding (does not persist — callers save() explicitly, as the
  // old window.HOTKEYS[...] = ...; saveHotkeys() sequence did).
  set(id, combo) {
    this.#current[id] = combo;
  }

  // Reset one binding to its default.
  reset(id) {
    this.#current[id] = this.#defaults[id];
  }

  // Reset every binding to defaults (matches Object.assign(HOTKEYS, DEFAULTS)).
  resetAll() {
    Object.assign(this.#current, this.#defaults);
  }

  // Live [id, combo] entries (for the conflict scan in the settings editor).
  entries() {
    return Object.entries(this.#current);
  }

  // Persist the current bindings under the same key/shape as before.
  save() {
    if (typeof localStorage === 'undefined') return;
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(this.#current));
    } catch {
      /* ignore */
    }
  }

  // Update every .ctx-hotkey[data-hk] element so context menus reflect current
  // bindings. No-op-safe when there's no document (Node import).
  updateCtxHints() {
    if (typeof document === 'undefined') return;
    document.querySelectorAll('[data-hk]').forEach(el => {
      const id = el.dataset.hk;
      if (this.#current[id]) el.textContent = this.#current[id];
    });
  }
}

// The single shared hotkey registry.
export const hotkeys = new Hotkeys();
