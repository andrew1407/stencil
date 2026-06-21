import { core } from './core/stencilCore.js';
// ── Utilities (consolidated): DOM, mount, notify, geometry, color, hotkeys ──
// The mutable hotkey registry lives in ./core/hotkeys.js (the `hotkeys`
// singleton); the pure parse/match helpers below stay here.

// ── Small DOM helpers ───────────────────────────────────────────
// Guarded value-set: assign to an element's .value only if it exists.
export const setVal = (id, value) => {
  const el = document.getElementById(id);
  if (el) el.value = value;
};
// Check the radio in a named group whose value matches.
export const setRadioGroup = (name, value) => {
  document.querySelectorAll(`input[name="${name}"]`).forEach(r => { r.checked = r.value === value; });
};
// Mount an HTML string into a parent element so getElementById works
// synchronously afterwards. Appends without nuking pre-existing children.
export const mountHTML = (parent, html) => {
  parent.insertAdjacentHTML('beforeend', html);
};

// ── Length units ────────────────────────────────────────────────
// The model always stores lengths in centimetres; `unit` ('cm' | 'in') only
// controls how they are shown/entered. 1 inch = 2.54 cm.
export const CM_PER_INCH = 2.54;
// cm → displayed unit value.
export const cmToUnit = (cm, unit) => (unit === 'in' ? cm / CM_PER_INCH : cm);
// displayed unit value → cm (for inputs the user types in the active unit).
export const unitToCm = (val, unit) => (unit === 'in' ? val * CM_PER_INCH : val);
// Short label for the active unit.
export const unitLabel = (unit) => (unit === 'in' ? 'in' : 'cm');

const IMPERIAL_REGIONS = new Set(['US', 'LR', 'MM']);
/**
 * Seed the initial display unit from locale (a saved/typed preference overrides).
 * No "measurement system" web API exists, so the region is derived via Intl.Locale
 * (maximize() resolves bare "en" → US); only US/Liberia/Myanmar get inches. Never throws.
 * @param {Navigator} [nav] - Navigator-like object; defaults to globalThis.navigator.
 * @returns {'cm'|'in'} The locale-appropriate display unit.
 */
export const defaultUnitFromLocale = (
  nav = (typeof globalThis !== 'undefined' ? globalThis.navigator : undefined),
) => {
  try {
    // No usable locale tag → fall back to metric (the international default),
    // not to a US-biased guess.
    const tag = (nav && nav.languages && nav.languages[0]) || (nav && nav.language) || '';
    if (!tag) return 'cm';
    const loc = new Intl.Locale(tag);
    const region = (loc.region || loc.maximize().region || '').toUpperCase();
    return IMPERIAL_REGIONS.has(region) ? 'in' : 'cm';
  } catch {
    return 'cm';
  }
};

// ── Inline name editor (shared: topbar + projects list) ─────────
/**
 * Wire a live-validated inline rename editor. Enables ✓ only when the trimmed
 * value has changed AND is valid (the rejection reason shows on ✓'s tooltip when
 * disabled). Enter commits (when ✓ is enabled), Escape cancels. A mousedown
 * preventDefault keeps the input focused so the click fires before any blur
 * handler (callers wire blur→cancel for click-away discard).
 * @param {HTMLInputElement} input - The editable name field.
 * @param {HTMLElement} acceptBtn - The ✓ commit button.
 * @param {HTMLElement} cancelBtn - The ✗ cancel button.
 * @param {object} opts
 * @param {() => string} opts.current - Returns the current (saved) name.
 * @param {(v: string) => {ok: boolean, reason?: string}} opts.validate - Validates a candidate name.
 * @param {(v: string) => void} opts.commit - Commits an accepted name.
 * @param {() => void} opts.cancel - Cancels editing.
 * @param {boolean} [opts.alwaysShow=false] - Keep ✓/✗ visible always (projects
 *   list); otherwise they appear only once the value differs (topbar).
 * @returns {{refresh: () => void}} Handle exposing a `refresh` to re-run validation.
 */
export const wireNameEditor = (input, acceptBtn, cancelBtn, { current, validate, commit, cancel, alwaysShow = false }) => {
  const refresh = () => {
    const v = input.value.trim();
    const changed = v !== (current() || '');
    if (!alwaysShow) {
      acceptBtn.style.display = changed ? '' : 'none';
      cancelBtn.style.display = changed ? '' : 'none';
    }
    if (!changed) { acceptBtn.disabled = true; acceptBtn.title = 'No change'; return; }
    const res = validate(v) || { ok: true, reason: '' };
    acceptBtn.disabled = !res.ok;
    acceptBtn.title = res.ok ? 'Save name (Enter)' : res.reason;
  };
  const doCommit = () => {
    const v = input.value.trim();
    if (!acceptBtn.disabled && v !== (current() || '')) commit(v);
  };
  input.addEventListener('input', refresh);
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); doCommit(); }
    else if (e.key === 'Escape') { e.preventDefault(); cancel(); }
  });
  for (const b of [acceptBtn, cancelBtn]) b.addEventListener('mousedown', (e) => e.preventDefault());
  acceptBtn.addEventListener('click', doCommit);
  cancelBtn.addEventListener('click', () => cancel());
  refresh();
  return { refresh };
};

// ── Notification balloon ────────────────────────────────────────
// Delegates to the <stencil-notifications> custom element, which owns the
// show/auto-hide logic. Kept as a free function so existing import sites work.
export const notify = (msg, type = 'ok') => {
  const el = document.getElementById('notify-balloon');
  if (el && typeof el.notify === 'function') el.notify(msg, type);
};

/**
 * Whether the browser can share FILES via the Web Share API (most desktop
 * browsers cannot, even if navigator.share exists for text/URLs). Used to decide
 * whether to render the Share-image action at all.
 * @returns {boolean} True when file sharing is supported.
 */
export const supportsShareFiles = () => {
  try {
    return !!(navigator.canShare &&
      navigator.canShare({ files: [new File([], 'x.png', { type: 'image/png' })] }));
  } catch {
    return false;
  }
};

// ── Geometry helpers (pure) ─────────────────────────────────────
/**
 * Distance from point (px,py) to the segment a→b. Delegates to the shared C++
 * core (wasm) when loaded; the JS body is the reference + fallback.
 * @param {number} px - Point x.
 * @param {number} py - Point y.
 * @param {{x: number, y: number}} a - Segment start.
 * @param {{x: number, y: number}} b - Segment end.
 * @returns {number} Shortest distance from the point to the segment.
 */
export const distToSegment = core.bind('distToSegment', (px, py, a, b) => {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const lenSq = dx * dx + dy * dy;
  if (lenSq === 0) return Math.hypot(px - a.x, py - a.y);
  const t = Math.max(0, Math.min(1, ((px - a.x) * dx + (py - a.y) * dy) / lenSq));
  return Math.hypot(px - (a.x + t * dx), py - (a.y + t * dy));
});

// ── Color helpers (pure) ────────────────────────────────────────
/**
 * Convert "#rrggbb" + alpha → "rgba(...)". Values that are already rgba/named
 * pass through unchanged.
 * @param {string} hex - A "#rrggbb" color (or any non-hex string to pass through).
 * @param {number} alpha - Alpha channel in [0, 1].
 * @returns {string} An "rgba(r,g,b,alpha)" string, or `hex` unchanged.
 */
export const hexToRgba = (hex, alpha) => {
  if (typeof hex !== 'string' || hex[0] !== '#' || hex.length < 7) return hex;
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  return `rgba(${r},${g},${b},${alpha})`;
};

/**
 * Parse "#rrggbb" → { r, g, b }. Delegates to the shared C++ core (wasm) when
 * loaded and the string is a valid 7-char hex; the JS body is reference + fallback.
 * @param {string} hex - A "#rrggbb" color string.
 * @returns {{r: number, g: number, b: number}} The 0–255 channel values.
 */
export const parseHex = hex => {
  const fn = core.op('parseHex');
  if (fn) {
    const rgb = fn(hex);
    if (rgb) return rgb;
  }
  return {
    r: parseInt(hex.slice(1, 3), 16),
    g: parseInt(hex.slice(3, 5), 16),
    b: parseInt(hex.slice(5, 7), 16),
  };
};

// ── Hotkey parsing / matching (pure) ────────────────────────────
/**
 * Normalize a KeyboardEvent code/key into a bare key token.
 * @param {string} code - KeyboardEvent.code (e.g. "KeyA", "Digit0", "ArrowUp").
 * @param {string} key - KeyboardEvent.key, used as a fallback when `code` is empty.
 * @returns {string} The normalized key (e.g. "A", "0", "ArrowUp").
 */
export const normalizeKey = (code, key) => {
  if (!code) return key || '';
  if (code.startsWith('Key')) return code.slice(3);     // KeyA  -> A
  if (code.startsWith('Digit')) return code.slice(5);   // Digit0 -> 0
  return code;                                          // ArrowUp / Numpad0 / F2 …
};
/**
 * Parse a "Ctrl+Shift+Z" combo string into a structured descriptor.
 * @param {string} str - The combo string; the last "+"-segment is the key.
 * @returns {{ctrl: boolean, shift: boolean, alt: boolean, meta: boolean, key: string}|null}
 *   The parsed combo, or null when `str` is empty/invalid.
 */
export const parseHotkey = str => {
  if (!str) return null;
  const parts = str.split('+').map(p => p.trim()).filter(Boolean);
  if (parts.length === 0) return null;
  const key = parts[parts.length - 1];
  const mods = parts.slice(0, -1).map(p => p.toLowerCase());
  return {
    ctrl: mods.includes('ctrl'), shift: mods.includes('shift'),
    alt: mods.includes('alt'), meta: mods.includes('meta'), key,
  };
};
/**
 * Test whether a KeyboardEvent matches a combo string (modifiers + key).
 * @param {KeyboardEvent} e - The keyboard event.
 * @param {string} hkStr - A combo string such as "Ctrl+Shift+Z".
 * @returns {boolean} True when the event exactly matches the combo.
 */
export const matchHotkey = (e, hkStr) => {
  const h = parseHotkey(hkStr);
  if (!h) return false;
  if (!!e.ctrlKey !== h.ctrl) return false;
  if (!!e.shiftKey !== h.shift) return false;
  if (!!e.altKey !== h.alt) return false;
  if (!!e.metaKey !== h.meta) return false;
  const norm = normalizeKey(e.code, e.key);
  return norm.toLowerCase() === h.key.toLowerCase();
};
/**
 * Build a canonical combo string from a KeyboardEvent (for capturing rebinds).
 * @param {KeyboardEvent} e - The keyboard event.
 * @returns {string|null} A combo like "Ctrl+Shift+A", or null for a bare modifier press.
 */
export const comboFromEvent = e => {
  if (['Control', 'Shift', 'Alt', 'Meta'].includes(e.key)) return null;
  const parts = [];
  if (e.ctrlKey) parts.push('Ctrl');
  if (e.altKey) parts.push('Alt');
  if (e.shiftKey) parts.push('Shift');
  if (e.metaKey) parts.push('Meta');
  parts.push(normalizeKey(e.code, e.key));
  return parts.join('+');
};
// ── Platform detection / Mac-relative hotkeys (pure) ────────────
/**
 * Whether the platform is macOS. Prefers the modern userAgentData.platform hint,
 * falls back to navigator.platform / userAgent matching /Mac/i. Safe when `nav`
 * is undefined (returns false) so it can be called in Node without throwing.
 * @param {Navigator} [nav] - Navigator-like object; defaults to globalThis.navigator.
 * @returns {boolean} True on macOS.
 */
export const isMacPlatform = (nav = (typeof globalThis !== 'undefined' ? globalThis.navigator : undefined)) => {
  if (!nav) return false;
  const uaPlat = nav.userAgentData && nav.userAgentData.platform;
  if (typeof uaPlat === 'string') return /mac/i.test(uaPlat);
  if (typeof nav.platform === 'string' && /mac/i.test(nav.platform)) return true;
  if (typeof nav.userAgent === 'string' && /Mac/i.test(nav.userAgent)) return true;
  return false;
};

/**
 * Best-effort desktop OS for choosing a download link. Android matches "Linux"
 * in its UA, so it is excluded explicitly.
 * @param {Navigator} [nav] - Navigator-like object; defaults to globalThis.navigator.
 * @returns {'mac'|'windows'|'linux'|null} The OS, or null when unknown (mobile, Node).
 */
export const detectDesktopOS = (nav = (typeof globalThis !== 'undefined' ? globalThis.navigator : undefined)) => {
  if (!nav) return null;
  const uaPlat = (nav.userAgentData && nav.userAgentData.platform) || nav.platform || '';
  const hay = `${uaPlat} ${nav.userAgent || ''}`;
  if (/android/i.test(hay)) return null;
  if (isMacPlatform(nav)) return 'mac';
  if (/win/i.test(hay)) return 'windows';
  if (/linux|x11/i.test(hay)) return 'linux';
  return null;
};

/**
 * Rewrite a canonical combo for the platform. On Mac, Ctrl-based editing shortcuts
 * (undo/redo/copy/paste, etc.) map to ⌘, so the Ctrl token → Meta; and the primary
 * delete key emits Backspace (⌫), so a Delete key token → Backspace. Token-aware,
 * case-insensitive, idempotent.
 * @param {string} combo - The canonical combo string.
 * @param {boolean} isMac - Whether to apply the Mac remapping.
 * @returns {string} The platformized combo (unchanged when `!isMac`).
 */
export const platformizeCombo = (combo, isMac) => {
  if (!isMac || !combo) return combo;
  return combo.split('+')
    .map(p => {
      const t = p.trim().toLowerCase();
      if (t === 'ctrl') return 'Meta';
      if (t === 'delete') return 'Backspace';
      return p;
    })
    .join('+');
};

/**
 * Render a combo for DISPLAY only (storage stays canonical). On Mac, tokens map to
 * Apple symbols in ⌃⌥⇧⌘ order then the key, joined with no separator
 * ("Meta+Shift+Z" → "⇧⌘Z", "Alt+ArrowUp" → "⌥↑").
 * @param {string} combo - The canonical combo string.
 * @param {boolean} isMac - Whether to render Apple glyphs.
 * @returns {string} The display string (the canonical form unchanged when `!isMac`).
 */
export const formatCombo = (combo, isMac) => {
  if (!isMac || !combo) return combo;
  const parts = combo.split('+').map(p => p.trim()).filter(Boolean);
  if (parts.length === 0) return combo;
  const key = parts[parts.length - 1];
  const mods = new Set(parts.slice(0, -1).map(p => p.toLowerCase()));
  const symFor = m => ({
    ctrl: '⌃', control: '⌃', alt: '⌥', option: '⌥',
    shift: '⇧', meta: '⌘', cmd: '⌘', command: '⌘',
  })[m] || '';
  // Apple convention: ⌃ ⌥ ⇧ ⌘ then the key.
  let out = '';
  for (const m of ['ctrl', 'control', 'alt', 'option', 'shift', 'meta', 'cmd', 'command']) {
    if (mods.has(m)) { out += symFor(m); mods.delete(m); }
  }
  const glyphs = {
    ArrowUp: '↑', ArrowDown: '↓', ArrowLeft: '←', ArrowRight: '→',
    Backspace: '⌫', Delete: '⌦',
  };
  return out + (glyphs[key] || key);
};

// ── Unified control tooltip ─────────────────────────────────────
/**
 * Compose an element's `title` from 3 optional parts so toolbar/menu tooltips
 * stay consistent and live:
 *   • base: data-title wins; else the current title with a trailing "(…)" hotkey
 *     and prior "— reason" line stripped (cached into data-title for stability).
 *   • hotkey: data-hk-title names a hotkey id; its combo is appended as " (…)",
 *     platform-formatted via the injected getCombo (avoids importing hotkeys here,
 *     since that module imports this one).
 *   • reason: data-disabled-reason, shown as a "— reason" line only while the
 *     element is disabled or has the `ctx-disabled` class.
 * @param {HTMLElement} el - The control whose tooltip is composed.
 * @param {boolean} isMac - Whether to render Mac hotkey glyphs.
 * @param {(hkId: string) => string} [getCombo] - Resolves a hotkey id to a combo string.
 * @returns {string} The composed tooltip text.
 */
export const composeControlTitle = (el, isMac, getCombo) => {
  let base = el.dataset.title;
  if (base == null) {
    base = (el.getAttribute('title') || '')
      .replace(/\n[\s\S]*$/, '')          // drop any existing reason line
      .replace(/\s*\([^)]*\)\s*$/, '');   // drop any trailing "(combo)"
    el.dataset.title = base;
  }
  let out = base;
  const hkId = el.dataset.hkTitle;
  if (hkId && getCombo) {
    const combo = getCombo(hkId);
    if (combo) out += `${out ? ' ' : ''}(${formatCombo(combo, isMac)})`;
  }
  const off = el.disabled === true || el.classList.contains('ctx-disabled');
  if (off && el.dataset.disabledReason) out += `${out ? '\n' : ''}— ${el.dataset.disabledReason}`;
  return out;
};

/**
 * Whether an event target is a text-entry control (so global hotkeys should be
 * suppressed). Checkbox/radio/file/color/button inputs are NOT typing targets.
 * @param {EventTarget|null} t - The element to test.
 * @returns {boolean} True for textareas, text-like inputs, selects, and contentEditable.
 */
export const isTypingTarget = t => {
  if (!t) return false;
  const tag = (t.tagName || '').toLowerCase();
  if (tag === 'textarea') return true;
  if (tag === 'select') return true;
  if (tag === 'input') {
    const ty = (t.type || '').toLowerCase();
    return !(ty === 'checkbox' || ty === 'radio' || ty === 'file' || ty === 'color' || ty === 'button');
  }
  return t.isContentEditable === true;
};
