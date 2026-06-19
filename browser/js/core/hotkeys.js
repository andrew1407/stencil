// ── Hotkey registry singleton ───────────────────────────────────
// Mutable keyboard-shortcut bindings as #private fields of one `hotkeys` instance (no window
// globals). Defaults from hotkeysConfig.json, overrides merged from localStorage
// 'drawingApp_hotkeys'. localStorage/DOM access guarded so importing in Node stays inert.
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };
import { platformizeCombo, isMacPlatform, formatCombo, composeControlTitle } from '../utils.js';

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

  // Patch every [data-hk-title] element's `title` to the platform-formatted current binding
  // (keeping any disabled-reason line). composeControlTitle owns base/hotkey/reason composition,
  // shared with DrawingApp.updateButtons → identical, macOS-correct (⌥/⇧/⌘) tooltips, live across rebinds.
  updateHotkeyTitles() {
    if (typeof document === 'undefined') return;
    document.querySelectorAll('[data-hk-title]').forEach(el => {
      el.title = composeControlTitle(el, this.#isMac, id => this.#current[id]);
    });
  }
}

// The single shared hotkey registry.
export const hotkeys = new Hotkeys();
