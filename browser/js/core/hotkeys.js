// Hotkey registry singleton: defaults from hotkeysConfig.json, overrides merged from
// localStorage 'drawingApp_hotkeys'; importing in Node stays inert.
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };
import { platformizeCombo, isMacPlatform, formatCombo, composeControlTitle, setHtml } from '../utils.js';
import { keysHtml } from '../ui/tip/tipContent.js';

const STORAGE_KEY = 'drawingApp_hotkeys';

class Hotkeys {
  #isMac = isMacPlatform();
// The reset target, platformized: on Mac the canonical Ctrl combos become Meta (⌘).
  #defaults = Object.freeze(Object.fromEntries(
    HOTKEY_DEFS.map(h => [h.id, platformizeCombo(h.default, this.#isMac)])));
  #current;

  constructor() {
    this.#current = { ...this.#defaults };
    if (typeof localStorage === 'undefined') return;
    try {
      const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}');
// platformizeCombo is idempotent; a legacy Ctrl override maps to Meta on Mac.
      for (const k in saved)
        if (k in this.#current) this.#current[k] = platformizeCombo(saved[k], this.#isMac);
    } catch {
      /* ignore */
    }
  }

  get(id) {
    return this.#current[id];
  }

  getDefault(id) {
    return this.#defaults[id];
  }

  get isMac() {
    return this.#isMac;
  }

// Does not persist — callers save() explicitly.
  set(id, combo) {
    this.#current[id] = combo;
  }

  reset(id) {
    this.#current[id] = this.#defaults[id];
  }

  resetAll() {
    Object.assign(this.#current, this.#defaults);
  }

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

// "<label> (<combo>)", platform-aware; just the label when the id has no binding.
  hkTitle(label, id) {
    const combo = this.#current[id];
    return combo ? `${label} (${formatCombo(combo, this.#isMac)})` : label;
  }

// Memoized: menus poll updateCtxHints every 120ms (contextMenu.js LIVE_SYNC_INTERVAL_MS).
  #hintHtml = new Map();
  #comboHtml(combo) {
    let html = this.#hintHtml.get(combo);
    if (html === undefined) {
      html = keysHtml(formatCombo(combo, this.#isMac), this.#isMac);
      this.#hintHtml.set(combo, html);
    }
    return html;
  }

// Keycaps (tipContent.js keysHtml) for every .ctx-hotkey[data-hk]. setHtml writes only on a
// real change, so a hovered row's keycaps are never rebuilt under the pointer.
  updateCtxHints() {
    if (typeof document === 'undefined') return;
    document.querySelectorAll('[data-hk]').forEach(el => {
      const id = el.dataset.hk;
      if (!this.#current[id]) return;
      setHtml(el, this.#comboHtml(this.#current[id]));
    });
    this.updateHotkeyTitles();
  }

// composeControlTitle owns base/hotkey/reason composition, shared with DrawingApp.updateButtons.
  updateHotkeyTitles() {
    if (typeof document === 'undefined') return;
    document.querySelectorAll('[data-hk-title]').forEach(el => {
      el.dataset.tip = composeControlTitle(el, this.#isMac, id => this.#current[id]);
    });
  }
}

export const hotkeys = new Hotkeys();
