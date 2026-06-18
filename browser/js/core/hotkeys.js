// ── Hotkey registry singleton ───────────────────────────────────
// Owns the mutable keyboard-shortcut bindings as #private fields of one `hotkeys`
// instance (no window globals). Defaults from hotkeysConfig.json, overrides merged
// from localStorage 'drawingApp_hotkeys'. localStorage/DOM access is guarded by
// typeof window/document/localStorage so importing this leaf module in Node stays inert.
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };
import { platformizeCombo, isMacPlatform, formatCombo } from '../utils.js';

const STORAGE_KEY = 'drawingApp_hotkeys';

class Hotkeys {
  // Whether the active platform is macOS (decided once at construction).
  #isMac = isMacPlatform();
  // Frozen id → default-combo map (the reset target). Defaults are platformized
  // so on Mac the canonical Ctrl-based combos become Meta-based (⌘).
  #defaults = Object.freeze(Object.fromEntries(
    HOTKEY_DEFS.map(h => [h.id, platformizeCombo(h.default, this.#isMac)])));
  // Live id → combo map (mutable; starts from defaults, then merges saved).
  #current;

  constructor() {
    this.#current = { ...this.#defaults };
    // Merge persisted overrides only in the browser.
    if (typeof localStorage === 'undefined') return;
    try {
      const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}');
      // platformizeCombo is idempotent, so re-applying to an already-Meta
      // override is a no-op; a legacy Ctrl override gets mapped to Meta on Mac.
      for (const k in saved)
        if (k in this.#current) this.#current[k] = platformizeCombo(saved[k], this.#isMac);
    } catch {
      /* ignore */
    }
  }

  // Current combo for an id (undefined if unknown).
  get(id) {
    return this.#current[id];
  }

  // Default combo for an id (platformized: Meta-based on Mac).
  getDefault(id) {
    return this.#defaults[id];
  }

  // True on macOS — consumers use this to drive formatCombo for display.
  get isMac() {
    return this.#isMac;
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

  // Format "<label> (<combo>)" for a tooltip, platform-aware (⌥R on Mac, Alt+R
  // elsewhere). Returns just the label when the id has no binding.
  hkTitle(label, id) {
    const combo = this.#current[id];
    return combo ? `${label} (${formatCombo(combo, this.#isMac)})` : label;
  }

  // Update every .ctx-hotkey[data-hk] element so context menus reflect current
  // bindings. No-op-safe when there's no document (Node import).
  updateCtxHints() {
    if (typeof document === 'undefined') return;
    document.querySelectorAll('[data-hk]').forEach(el => {
      const id = el.dataset.hk;
      if (this.#current[id]) el.textContent = formatCombo(this.#current[id], this.#isMac);
    });
    this.updateHotkeyTitles();
  }

  // Patch every [data-hk-title] element's `title` tooltip to the platform-formatted
  // current binding. data-hk-title = hotkey id; data-hk-label = base tooltip text
  // (falls back to the element's existing title with any "(…)" suffix stripped).
  // Keeps button tooltips correct on macOS (⌥/⇧/⌘) and live across rebinds.
  updateHotkeyTitles() {
    if (typeof document === 'undefined') return;
    document.querySelectorAll('[data-hk-title]').forEach(el => {
      const id = el.dataset.hkTitle;
      if (!this.#current[id]) return;
      const label = el.dataset.hkLabel
        || (el.dataset.hkLabel = (el.getAttribute('title') || '').replace(/\s*\([^)]*\)\s*$/, ''));
      el.title = `${label} (${formatCombo(this.#current[id], this.#isMac)})`;
    });
  }
}

// The single shared hotkey registry.
export const hotkeys = new Hotkeys();
