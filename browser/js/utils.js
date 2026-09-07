import { core } from './core/stencilCore.js';
// ── Utilities (consolidated): DOM, mount, notify, geometry, color, hotkeys ──
// The mutable hotkey registry lives in ./core/hotkeys.js (the `hotkeys`
// singleton); the pure parse/match helpers below stay here.

// ── Small DOM helpers ───────────────────────────────────────────
export const setVal = (id, value) => {
  const el = document.getElementById(id);
  if (el) el.value = value;
};
export const setRadioGroup = (name, value) => {
  document.querySelectorAll(`input[name="${name}"]`).forEach(r => { r.checked = r.value === value; });
};
// Mount an HTML string into a parent element so getElementById works
// synchronously afterwards. Appends without nuking pre-existing children.
export const mountHTML = (parent, html) => {
  parent.insertAdjacentHTML('beforeend', html);
};
// Write innerHTML only when it CHANGED, compared against the string last WRITTEN — not
// against el.innerHTML: reading an SVG back re-serializes self-closing tags, so it never
// matches and an unconditional write would rebuild (and re-animate) the nodes per poll.
const lastHtml = new WeakMap();
export const setHtml = (el, html) => {
  if (!el || lastHtml.get(el) === html) return;
  el.innerHTML = html;
  lastHtml.set(el, html);
};
// Place a fixed-position cursor-follow popup down-right of (x, y): flipped to the
// cursor's other side when it would clip, then edge-clamped. `clampY` pins an
// overflowing box to the bottom edge instead of flipping above (the projects thumb
// zoom). Shared by exportPreview.js and projectsModal.js.
export const placeNearCursor = (el, x, y, { pad = 18, edge = 8, clampY = false } = {}) => {
  const { width: w, height: h } = el.getBoundingClientRect();
  let left = x + pad;
  let top = y + pad;
  if (left + w > window.innerWidth - edge) left = x - w - pad;
  if (top + h > window.innerHeight - edge) top = clampY ? window.innerHeight - edge - h : y - h - pad;
  el.style.left = `${Math.max(edge, left)}px`;
  el.style.top = `${Math.max(edge, top)}px`;
};

// ── Length units ────────────────────────────────────────────────
// The model always stores lengths in centimetres; `unit` ('cm' | 'in') only
// controls how they are shown/entered. 1 inch = 2.54 cm.
export const CM_PER_INCH = 2.54;
export const cmToUnit = (cm, unit) => (unit === 'in' ? cm / CM_PER_INCH : cm);
export const unitToCm = (val, unit) => (unit === 'in' ? val * CM_PER_INCH : val);
export const unitLabel = (unit) => (unit === 'in' ? 'in' : 'cm');

// True while a split compare view (vertical/horizontal) is actually showing. Shared by
// exportService/contextMenu/exportOptionsMenu/controlsBinder so the check can't drift.
export const isSplitCompare = (app) => app.compareMode === 'vertical' || app.compareMode === 'horizontal';

// Gates for the "Filter Only"/"Current" export-variant rows (contextMenu.js,
// exportOptionsMenu.js) — each would otherwise render byte-identical to a sibling variant.
export const hasActiveFilter = (app) => !!(app.imageFilter && app.imageFilter !== 'none');
export const hasAnyLines = (app) => !!(app.lines && app.lines.length > 0);

// ── Comparison view: is an image point in the EDITED half? ───────────────────
// Same geometry as the desktop's hover gate (mainWindow) — the two surfaces must agree
// point for point. `mode` is the EFFECTIVE mode (renderer.effectiveCompareMode), so the
// Alt+Shift+O peek is already folded in; callers pass the POINT's coords, not the cursor's.
export const compareEditedShows = (mode, split, x, y, imageW, imageH) => {
  if (mode === 'original') return false;
  if (mode !== 'vertical' && mode !== 'horizontal') return true;
  const f = Math.min(1, Math.max(0, Number.isFinite(split) ? split : 0.5));
  return mode === 'vertical' ? x >= imageW * f : y >= imageH * f;
};

const IMPERIAL_REGIONS = new Set(['US', 'LR', 'MM']);
// Seed the initial display unit from locale (a saved/typed preference overrides).
// No "measurement system" web API exists, so the region is derived via Intl.Locale
// (maximize() resolves bare "en" → US); only US/Liberia/Myanmar get inches. Never throws.
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
// Wire a live-validated inline rename editor. ✓ enables only when the trimmed value
// changed AND validates (rejection reason shows on ✓'s tooltip); Enter commits, Escape
// cancels. mousedown preventDefault keeps focus so the click fires before any blur
// handler (callers wire blur→cancel). `alwaysShow` keeps ✓/✗ visible (projects list);
// otherwise they appear once the value differs (topbar). Returns { refresh }.
export const wireNameEditor = (input, acceptBtn, cancelBtn, { current, validate, commit, cancel, alwaysShow = false }) => {
  const refresh = () => {
    const v = input.value.trim();
    const changed = v !== (current() || '');
    if (!alwaysShow) {
      acceptBtn.style.display = changed ? '' : 'none';
      cancelBtn.style.display = changed ? '' : 'none';
    }
    if (!changed) { acceptBtn.disabled = true; acceptBtn.dataset.title = 'No change'; return; }
    const res = validate(v) || { ok: true, reason: '' };
    acceptBtn.disabled = !res.ok;
    acceptBtn.dataset.title = res.ok ? 'Save name (Enter)' : res.reason;
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

// ── Coordinates-panel drag resizer ──────────────────────────────
// Resizes the coordinates panel by writing the shared `--coord-panel-width` CSS var
// (clamped); used by both the normal and fullscreen panels. Dragging LEFT widens.
// `onStart`/`onEnd` hook a drag (fullscreen pauses its auto-hide).
// The width is kept for THIS tab only (sessionStorage): a reload keeps it, a reopened
// app starts at the default again — the desktop relaunches at its default too.
const COORD_PANEL_WIDTH_KEY = 'drawingApp_coordPanelWidth';
// Never squeeze the canvas below this; under it the layout overflows rather than shrinks.
export const MIN_CANVAS_WIDTH = 320;

// Widest the panel may be in a window of `winW`: never past `maxFactor` of it, never past
// the 640 ceiling, never leaving less than MIN_CANVAS_WIDTH for the canvas. Pure → testable.
export const clampPanelWidth = (w, winW, maxFactor = 0.7) =>
  Math.max(240, Math.min(640, Math.round(winW * maxFactor), Math.max(0, winW - MIN_CANVAS_WIDTH), w));

export const wirePanelResizer = (resizer, panel, { maxFactor = 0.7, onStart, onEnd, restore = false } = {}) => {
  const clamp = (w) => clampPanelWidth(w, window.innerWidth, maxFactor);
  const setWidth = (w) => document.documentElement.style.setProperty('--coord-panel-width', clamp(w) + 'px');
  // The width survives a reload, so a panel dragged wide in a big window can come back
  // into a small one — and a window can be narrowed after the fact. Re-clamp on both,
  // against the live window, keeping the preference for when there is room for it again.
  // A panel the user never dragged has NO preference: it stays on the CSS default
  // (--coord-panel-default) rather than being pinned to whatever it measures right now.
  // Measuring it was the bug: below the stacking breakpoint the panel spans the window
  // under the canvas, and each resize fed that full width back as the "preference",
  // ratcheting an untouched panel up to its cap by the time the window was wide again.
  const applyStored = () => {
    let saved = NaN;
    try { saved = parseInt(sessionStorage.getItem(COORD_PANEL_WIDTH_KEY), 10); } catch { /* storage blocked */ }
    if (Number.isFinite(saved)) setWidth(saved);
    else document.documentElement.style.removeProperty('--coord-panel-width');
  };
  if (restore) {
    applyStored();
    window.addEventListener('resize', applyStored);
  }
  let startX = 0, startW = 0, dragging = false;
  const onMove = (e) => { if (dragging) setWidth(startW + (startX - e.clientX)); };
  const onUp = () => {
    if (!dragging) return;
    dragging = false;
    resizer.classList.remove('dragging');
    document.body.style.userSelect = '';
    document.removeEventListener('mousemove', onMove);
    document.removeEventListener('mouseup', onUp);
    try { sessionStorage.setItem(COORD_PANEL_WIDTH_KEY, String(Math.round(panel.getBoundingClientRect().width))); } catch { /* storage blocked */ }
    onEnd?.();
  };
  resizer.addEventListener('mousedown', (e) => {
    e.preventDefault();
    dragging = true;
    startX = e.clientX;
    startW = panel.getBoundingClientRect().width;
    resizer.classList.add('dragging');
    document.body.style.userSelect = 'none';
    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
    onStart?.();
  });
  return { isDragging: () => dragging };
};

// ── Notification balloon ────────────────────────────────────────
// Delegates to the <stencil-notifications> custom element, which owns the
// show/auto-hide logic. Kept as a free function so existing import sites work.
export const notify = (msg, type = 'ok', opts = undefined) => {
  const el = document.getElementById('notify-balloon');
  if (el && typeof el.notify === 'function') el.notify(msg, type, opts);
};

// Touch-like surface: phone-width viewport OR no-hover + coarse pointer. THE app-wide
// rule — never sniff the user agent. `mm` injectable for tests; no matchMedia (Node) = desktop.
export const PHONE_MEDIA = '(max-width: 680px)';
export const TOUCH_MEDIA = `${PHONE_MEDIA}, (hover: none) and (pointer: coarse)`;
export const isTouchLike = (mm = (typeof matchMedia !== 'undefined' ? matchMedia : null)) => {
  try { return !!mm && !!mm(TOUCH_MEDIA).matches; } catch { return false; }
};

// Scale any CanvasImageSource to ≤ maxEdge px on the long edge, encode as a data URL.
// Never upscales. THE shared frame for attachment downscaling, extension thumbnails,
// and video frame grabs — browser-only.
export const scaledDataUrl = (source, width, height, maxEdge, type, quality) => {
  const k = Math.min(1, maxEdge / Math.max(width, height));
  const c = document.createElement('canvas');
  c.width = Math.max(1, Math.round(width * k));
  c.height = Math.max(1, Math.round(height * k));
  c.getContext('2d').drawImage(source, 0, 0, c.width, c.height);
  return c.toDataURL(type, quality);
};

// Whether the browser can share FILES via the Web Share API (most desktop browsers
// cannot, even if navigator.share exists for text/URLs); gates the Share-image action.
export const supportsShareFiles = () => {
  try {
    return !!(navigator.canShare &&
      navigator.canShare({ files: [new File([], 'x.png', { type: 'image/png' })] }));
  } catch {
    return false;
  }
};

// ── Geometry helpers (pure) ─────────────────────────────────────
// Distance from point (px,py) to the segment a→b. Delegates to the shared C++
// core (wasm) when loaded; the JS body is the reference + fallback.
export const distToSegment = core.bind('distToSegment', (px, py, a, b) => {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const lenSq = dx * dx + dy * dy;
  if (lenSq === 0) return Math.hypot(px - a.x, py - a.y);
  const t = Math.max(0, Math.min(1, ((px - a.x) * dx + (py - a.y) * dy) / lenSq));
  return Math.hypot(px - (a.x + t * dx), py - (a.y + t * dy));
});

// The native colour picker opens beside the input's own box, so a hidden 1px input pops
// it at the page corner instead of at the button pressed. Pin the input under the button
// first. Used by every swatch that hides its input behind a button.
export const anchorPickerInput = (input, btn) => {
  const r = btn?.getBoundingClientRect?.();
  if (!r) return;
  input.style.position = 'fixed';
  input.style.left = `${Math.round(r.left)}px`;
  input.style.top = `${Math.round(r.bottom)}px`;
};

// ── CSS colours, with alpha ─────────────────────────────────────
// Colours are stored as CSS, written straight into a canvas context: `#rrggbb` opaque,
// `#rrggbbaa` translucent. <input type="color"> has no alpha byte, so the editors keep it
// in a separate opacity control and these two join the halves. core's parseHex checks
// `< 7`, so the CLI and pystencil read the RGB and ignore the alpha.
// Desktop twin: support/cssColor.hpp — keep the pair in step.

// Split a stored colour into the swatch's own 7-char hex and an opacity in 0..1. Anything
// that is not a hex colour (a CSS name, 'transparent') keeps its value and reads as opaque,
// so a control fed one still shows something sensible. Pure.
export const cssColorParts = (value) => {
  const v = typeof value === 'string' ? value.trim() : '';
  if (/^#[0-9a-fA-F]{8}$/.test(v))
    return { hex: v.slice(0, 7).toLowerCase(), alpha: parseInt(v.slice(7), 16) / 255 };
  if (/^#[0-9a-fA-F]{6}$/.test(v)) return { hex: v.toLowerCase(), alpha: 1 };
  return { hex: v, alpha: 1 };
};

// …and back. Fully opaque writes the plain `#rrggbb` every surface reads; only a real
// alpha adds the byte. Pure.
export const cssWithAlpha = (hex, alpha) => {
  const h = typeof hex === 'string' ? hex.trim().toLowerCase() : '';
  const a = Math.max(0, Math.min(1, Number(alpha)));
  if (!/^#[0-9a-f]{6}$/.test(h) || !Number.isFinite(a)) return hex;
  if (a >= 1) return h;
  return h + Math.round(a * 255).toString(16).padStart(2, '0');
};

// ── A colour control and its opacity box ────────────────────────────────────
// <input type="color"> cannot carry an alpha byte, so every editable colour in the app is
// TWO controls — a swatch and a 0-255 opacity box — that these two put together and take
// apart. One seam, so the selection panel, the toolbar binder and the fullscreen mirror
// all read and write the pair the same way.

// An opacity box's 0-255 byte as the 0..1 fraction cssWithAlpha wants. A blank or
// out-of-range box reads as fully opaque rather than making the line vanish. Pure.
export const alphaFraction = (input) => {
  const n = Number(input?.value);
  if (!Number.isFinite(n)) return 1;
  return Math.max(0, Math.min(255, n)) / 255;
};

// The pair as one CSS colour: `#rrggbb` opaque, `#rrggbbaa` otherwise.
export const readColorPair = (colorId, alphaId, doc = document) =>
  cssWithAlpha(doc.getElementById(colorId).value, alphaFraction(doc.getElementById(alphaId)));

// …and the way back: a stored colour split across the swatch and the opacity box.
export const writeColorPair = (colorId, alphaId, value, doc = document) => {
  const { hex, alpha } = cssColorParts(value);
  const swatch = doc.getElementById(colorId);
  if (swatch) swatch.value = hex;
  const box = doc.getElementById(alphaId);
  if (box) box.value = String(Math.round(alpha * 255));
};

// An area's fill is its swatch plus its alpha, and all the way down at 0 IS "no fill" —
// stored as NO_FILL, the value every surface reads as unfilled.
export const NO_FILL = 'transparent';
export const fillFromPair = (colorId, alphaId, doc = document) =>
  alphaFraction(doc.getElementById(alphaId)) <= 0 ? NO_FILL : readColorPair(colorId, alphaId, doc);

// Edges inclusive; takes anything with left/right/top/bottom (a DOMRect in practice).
export const pointInRect = (x, y, rect) =>
  x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;

// ── Scrollbar hover ──────────────────────────────────────────────
// Whether a pointer at (x, y) is on a scrollable element's overlay scrollbar strip: the
// last `strip` px along its right edge (vertical bar, when `canY`) or bottom edge
// (horizontal, when `canX`). Overlay bars take no layout space, so the strip is measured
// off the element's own box. Pure, so the geometry is unit-testable.
export const SCROLLBAR_STRIP_PX = 14;
export const scrollbarHit = (box, x, y, { canX = false, canY = false, strip = SCROLLBAR_STRIP_PX } = {}) => {
  if (!pointInRect(x, y, box)) return false;
  if (canY && x >= box.right - strip) return true;
  if (canX && y >= box.bottom - strip) return true;
  return false;
};
// Toggles `sb-hover` on `el` while the pointer rests on one of its scrollbars — the
// thumb takes the accent ONLY then (layout.css), not whenever the panel is hovered:
// the standard scrollbar-color property Chrome/Firefox read has no thumb-hover of its
// own, and a panel-wide hover made every scroll paint the thumb accent (user report).
export const wireScrollbarHover = (el) => {
  if (!el) return;
  const set = (on) => el.classList.toggle('sb-hover', on);
  el.addEventListener('mousemove', (e) => {
    set(scrollbarHit(el.getBoundingClientRect(), e.clientX, e.clientY, {
      canX: el.scrollWidth > el.clientWidth,
      canY: el.scrollHeight > el.clientHeight,
    }));
  });
  el.addEventListener('mouseleave', () => set(false));
};

// ── Color helpers (pure) ────────────────────────────────────────
// "#rrggbb" + alpha → "rgba(...)"; already-rgba/named values pass through unchanged.
export const hexToRgba = (hex, alpha) => {
  if (typeof hex !== 'string' || hex[0] !== '#' || hex.length < 7) return hex;
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  return `rgba(${r},${g},${b},${alpha})`;
};

// Parse "#rrggbb" → { r, g, b }. Delegates to the shared C++ core (wasm) when loaded
// and the string is a valid 7-char hex; the JS body is reference + fallback.
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
// KeyboardEvent code/key → bare key token; `key` is the fallback when `code` is empty.
export const normalizeKey = (code, key) => {
  if (!code) return key || '';
  if (code.startsWith('Key')) return code.slice(3);     // KeyA  -> A
  if (code.startsWith('Digit')) return code.slice(5);   // Digit0 -> 0
  return code;                                          // ArrowUp / Numpad0 / F2 …
};
// "Ctrl+Shift+Z" → {ctrl,shift,alt,meta,key}; last "+"-segment is the key; null when empty.
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
// Canonical combo from a KeyboardEvent (for capturing rebinds); null for a bare modifier press.
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
// Prefers userAgentData.platform, falls back to navigator.platform / userAgent /Mac/i.
// Safe (false) when `nav` is undefined so Node can call it.
export const isMacPlatform = (nav = (typeof globalThis !== 'undefined' ? globalThis.navigator : undefined)) => {
  if (!nav) return false;
  const uaPlat = nav.userAgentData && nav.userAgentData.platform;
  // Only trust userAgentData.platform when NON-EMPTY: some Chromium/reduced-UACH
  // contexts report '' even on macOS, which would short-circuit to a false negative.
  if (typeof uaPlat === 'string' && uaPlat) return /mac/i.test(uaPlat);
  if (typeof nav.platform === 'string' && /mac/i.test(nav.platform)) return true;
  if (typeof nav.userAgent === 'string' && /Mac/i.test(nav.userAgent)) return true;
  return false;
};

// Best-effort desktop OS for choosing a download link; null when unknown (mobile, Node).
// Android matches "Linux" in its UA, so it is excluded explicitly.
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

// Rewrite a canonical combo for the platform: on Mac, Ctrl → Meta (editing shortcuts live
// on ⌘) and Delete → Backspace (the primary delete key emits ⌫). Token-aware,
// case-insensitive, idempotent; unchanged when `!isMac`.
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

// Render a combo for DISPLAY only (storage stays canonical). On Mac, tokens map to Apple
// symbols in ⌃⌥⇧⌘ order then the key, no separator ("Meta+Shift+Z" → "⇧⌘Z").
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
// Compose an element's `title` from 3 optional parts: base (data-title, else the current
// title with trailing "(…)"/"— reason" stripped and cached), hotkey (data-hk-title id →
// " (…)" via the injected getCombo — hotkeys imports this module, so no direct import),
// and reason (data-disabled-reason, shown only while disabled). Context-menu rows carry
// no tooltip of their own and hide instead of disabling (js/ui/contextMenu.js), so the
// disabled branch here only ever fires for a real disabled button.
// The authored base lives in data-title; callers write the result to data-tip, which the
// custom tooltip (ui/controlTooltip.js) prefers. Never the native `title` — the app has none.
export const composeControlTitle = (el, isMac, getCombo) => {
  const base = el.dataset.title || '';
  let out = base;
  const hkId = el.dataset.hkTitle;
  if (hkId && getCombo) {
    const combo = getCombo(hkId);
    if (combo) out += `${out ? ' ' : ''}(${formatCombo(combo, isMac)})`;
  }
  if (el.disabled === true && el.dataset.disabledReason) out += `${out ? '\n' : ''}— ${el.dataset.disabledReason}`;
  return out;
};

// Text-entry target (global hotkeys suppressed): textareas, text-like inputs, selects,
// contentEditable. Checkbox/radio/file/color/button inputs are NOT typing targets.
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

// Copy shortcuts (Ctrl+C = image, Ctrl+Alt+C = layout) defer to the browser's native
// text copy while a non-empty selection exists, so Ctrl+C copies the TEXT, not the image.
export const hasTextSelection = () => {
  if (typeof window === 'undefined' || !window.getSelection) return false;
  const sel = window.getSelection();
  return !!sel && !sel.isCollapsed && sel.toString().trim().length > 0;
};

// ── Display-shortening for project / image names ─────────────────────────────
// For names interpolated into dialog sentences/toasts, where CSS text-overflow can't help.
// Middle ellipsis because both ends carry meaning: the head is what the user recognises,
// the tail holds the extension / "-copy" suffix that says WHICH item this is.
// Ported to extension/src/lib/displayName.js and desktop/src/support/displayName.hpp
// — keep the three behaviourally identical (same limit, same head/tail split).
export const NAME_DISPLAY_CHARS = 28;
export const shortName = (name, limit = NAME_DISPLAY_CHARS) => {
  const s = String(name ?? '');
  if (s.length <= limit) return s;
  // Reserve one char for the ellipsis; give the extra char to the head on odd splits.
  const keep = limit - 1;
  const head = Math.ceil(keep / 2);
  const tail = keep - head;
  return `${s.slice(0, head)}…${tail > 0 ? s.slice(-tail) : ''}`;
};
