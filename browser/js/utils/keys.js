// `key` is the fallback when `code` is empty.
export const normalizeKey = (code, key) => {
  if (!code) return key || '';
  if (code.startsWith('Key')) return code.slice(3);
  if (code.startsWith('Digit')) return code.slice(5);
  return code;
};
// "Ctrl+Shift+Z" → {ctrl,shift,alt,meta,key}; null when empty.
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
// Canonical combo from a KeyboardEvent; null for a bare modifier press.
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
// Prefers userAgentData.platform, then navigator.platform / userAgent; false without `nav` (Node).
export const isMacPlatform = (nav = (typeof globalThis !== 'undefined' ? globalThis.navigator : undefined)) => {
  if (!nav) return false;
  const uaPlat = nav.userAgentData && nav.userAgentData.platform;
// Some reduced-UACH contexts report '' even on macOS: only a NON-EMPTY value is trusted.
  if (typeof uaPlat === 'string' && uaPlat) return /mac/i.test(uaPlat);
  if (typeof nav.platform === 'string' && /mac/i.test(nav.platform)) return true;
  if (typeof nav.userAgent === 'string' && /Mac/i.test(nav.userAgent)) return true;
  return false;
};

// On Mac, Ctrl → Meta and Delete → Backspace (the primary delete key emits ⌫). Idempotent.
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

// DISPLAY only (storage stays canonical): Apple symbols in ⌃⌥⇧⌘ order, no separator.
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

// Callers write the composed tooltip to data-tip (ui/controlTooltip.js), never the native
// `title` — the app has none. The hotkey arrives via the injected getCombo.
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
