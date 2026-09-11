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
