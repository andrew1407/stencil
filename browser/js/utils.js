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

// Seed the initial display unit from locale (a saved/typed preference overrides).
// No "measurement system" web API exists, so derive the region via Intl.Locale
// (maximize() resolves bare tags like "en" → US); only US/Liberia/Myanmar get
// inches. Never throws.
const IMPERIAL_REGIONS = new Set(['US', 'LR', 'MM']);
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

// ── Notification balloon ────────────────────────────────────────
// Delegates to the <stencil-notifications> custom element, which owns the
// show/auto-hide logic. Kept as a free function so existing import sites work.
export const notify = (msg, type = 'ok') => {
  const el = document.getElementById('notify-balloon');
  if (el && typeof el.notify === 'function') el.notify(msg, type);
};

// ── Geometry helpers (pure) ─────────────────────────────────────
// Distance from point (px,py) to the segment a→b. Delegates to the shared C++
// core (wasm) when loaded; the JS below is the reference + fallback.
export const distToSegment = core.bind('distToSegment', (px, py, a, b) => {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const lenSq = dx * dx + dy * dy;
  if (lenSq === 0) return Math.hypot(px - a.x, py - a.y);
  const t = Math.max(0, Math.min(1, ((px - a.x) * dx + (py - a.y) * dy) / lenSq));
  return Math.hypot(px - (a.x + t * dx), py - (a.y + t * dy));
});

// ── Color helpers (pure) ────────────────────────────────────────
// Convert "#rrggbb" + alpha → "rgba(...)"; passes through values already rgba/named.
export const hexToRgba = (hex, alpha) => {
  if (typeof hex !== 'string' || hex[0] !== '#' || hex.length < 7) return hex;
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  return `rgba(${r},${g},${b},${alpha})`;
};

// Parse "#rrggbb" → { r, g, b }. Delegates to the shared C++ core (wasm) when
// loaded and the string is a valid 7-char hex; the JS below is reference + fallback.
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
export const normalizeKey = (code, key) => {
  if (!code) return key || '';
  if (code.startsWith('Key')) return code.slice(3);     // KeyA  -> A
  if (code.startsWith('Digit')) return code.slice(5);   // Digit0 -> 0
  return code;                                          // ArrowUp / Numpad0 / F2 …
};
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
// True on macOS. Prefers the modern userAgentData.platform hint, falls back to
// navigator.platform / userAgent matching /Mac/i. Safe when nav is undefined
// (returns false) so it can be imported/called in Node without throwing.
export const isMacPlatform = (nav = (typeof globalThis !== 'undefined' ? globalThis.navigator : undefined)) => {
  if (!nav) return false;
  const uaPlat = nav.userAgentData && nav.userAgentData.platform;
  if (typeof uaPlat === 'string') return /mac/i.test(uaPlat);
  if (typeof nav.platform === 'string' && /mac/i.test(nav.platform)) return true;
  if (typeof nav.userAgent === 'string' && /Mac/i.test(nav.userAgent)) return true;
  return false;
};

// Best-effort desktop OS for choosing a download: 'mac'/'windows'/'linux', or
// null when unknown (mobile, Node) so the caller uses a generic releases link.
// Android matches "Linux" in its UA, so it's excluded explicitly.
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

// Rewrite a canonical combo for the active platform. On Mac the app's Ctrl-based
// editing shortcuts (undo/redo/copy/paste, etc.) map to ⌘, so we swap the Ctrl
// token to Meta. Token-aware (splits on '+'), case-insensitive on "Ctrl", and
// idempotent (an already-Meta combo is unchanged). No-op when isMac is false.
export const platformizeCombo = (combo, isMac) => {
  if (!isMac || !combo) return combo;
  return combo.split('+')
    .map(p => (p.trim().toLowerCase() === 'ctrl' ? 'Meta' : p))
    .join('+');
};

// Render a combo for DISPLAY only (storage stays canonical). On Mac, map tokens
// to Apple symbols in the conventional ⌃⌥⇧⌘ order followed by the key, joined
// with no separator (e.g. "Meta+Shift+Z" → "⇧⌘Z", "Alt+ArrowUp" → "⌥↑"). On
// non-Mac, the canonical "Ctrl+Shift+Z" form is returned unchanged.
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
  const arrows = { ArrowUp: '↑', ArrowDown: '↓', ArrowLeft: '←', ArrowRight: '→' };
  return out + (arrows[key] || key);
};

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
