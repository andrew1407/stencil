// ── Hotkey registry singleton ───────────────────────────────────
// Mutable keyboard-shortcut bindings as #private fields of one `hotkeys` instance (no window
// globals). Defaults from hotkeysConfig.json, overrides merged from localStorage
// 'drawingApp_hotkeys'. localStorage/DOM access guarded so importing in Node stays inert.
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };
import { platformizeCombo, isMacPlatform, formatCombo, composeControlTitle, setHtml } from '../utils.js';
import { keysHtml } from '../ui/tipContent.js';

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

  // Set/override a binding (does not persist — callers save() explicitly).
  set(id, combo) {
    this.#current[id] = combo;
  }

  // Reset one binding to its default.
  reset(id) {
    this.#current[id] = this.#defaults[id];
  }

  // Reset every binding to defaults.
  resetAll() {
    Object.assign(this.#current, this.#defaults);
  }

  // Live [id, combo] entries (for the conflict scan in the settings editor).
  entries() {
    return Object.entries(this.#current);
  }

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

  // Keycap markup per combo, memoized: menus poll updateCtxHints every 120ms
  // (contextMenu.js LIVE_SYNC_INTERVAL_MS), and the combos rarely change.
  #hintHtml = new Map();
  #comboHtml(combo) {
    let html = this.#hintHtml.get(combo);
    if (html === undefined) {
      html = keysHtml(formatCombo(combo, this.#isMac), this.#isMac);
      this.#hintHtml.set(combo, html);
    }
    return html;
  }

  // Update every .ctx-hotkey[data-hk] element so context menus reflect current
  // bindings — as bordered keycaps (tipContent.js keysHtml), the same markup the
  // floating tooltip's shortcut uses. No-op-safe when there's no document (Node import).
  // setHtml (utils.js) writes only on a real change, so a hovered row's keycaps are
  // never rebuilt under the pointer (which would loop their once-per-hover shake).
  updateCtxHints() {
    if (typeof document === 'undefined') return;
    document.querySelectorAll('[data-hk]').forEach(el => {
      const id = el.dataset.hk;
      if (!this.#current[id]) return;
      setHtml(el, this.#comboHtml(this.#current[id]));
    });
    this.updateHotkeyTitles();
  }

  // Patch every [data-hk-title] element's data-tip to the platform-formatted current binding
  // (keeping any disabled-reason line). composeControlTitle owns base/hotkey/reason composition,
  // shared with DrawingApp.updateButtons → identical, macOS-correct (⌥/⇧/⌘) tooltips, live across rebinds.
  updateHotkeyTitles() {
    if (typeof document === 'undefined') return;
    document.querySelectorAll('[data-hk-title]').forEach(el => {
      el.dataset.tip = composeControlTitle(el, this.#isMac, id => this.#current[id]);
    });
  }
}

// The single shared hotkey registry.
export const hotkeys = new Hotkeys();
