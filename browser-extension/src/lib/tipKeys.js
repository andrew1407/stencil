// ── Rich control tooltips: the keys half ────────────────────────────────────
// Byte-pinned PORT of browser/js/ui/tipKeys.js: which strings ARE keys, and the caps they draw.

import { escapeHtml } from './escapeHtml.js';

// Modifier and key vocabulary, case-sensitive on purpose: a bare word is a key only in a key
// context (KEY_PROSE below) or inside a trailing "(…)" of nothing but keys — never a verb.
const MOD = 'Ctrl|Control|Cmd|Command|Meta|Win|Alt|Option|Shift';
const NAMED = 'Enter|Return|Escape|Esc|Tab|Space|Backspace|Delete|Del|Home|End|PageUp|PageDown|Arrow(?:Up|Down|Left|Right)|F\\d{1,2}';
// The key glyphs a platform may hand us instead of a word. Qt's QKeySequence::NativeText emits
// these on macOS (⎋ ⇥ ⇞), so the set must cover them or those shortcuts never become keycaps.
const KEYGLYPH = '[⌫⌦↑↓←→⎋⇥↵⌤⇞⇟↖↘␣]';
// What can end a combo: a named key, a modifier, a gesture word, a single character, a glyph or
// punctuation. The gesture word comes BEFORE the character so "Alt+click" is not "Alt+c"+"lick".
const ATOM = `(?:${NAMED}|${MOD}|[a-z][a-z-]{1,11}|[A-Za-z0-9]|${KEYGLYPH}|[-+=\\[\\]/\\\\.,;'\`])`;
// Nothing may run on past the key, or half a word would end up wearing a keycap.
const END = '(?![\\w-])';
// "Ctrl+Shift+Z" / "Alt+0" / "Shift+click"
const CHAINED = `(?:(?:${MOD})\\+)+${ATOM}${END}`;
// Mac display form: a run of Apple glyphs then the key ("⇧⌘Z", "⌥↑")
const GLYPHS = `[⌃⌥⇧⌘]+${ATOM}${END}`;
// A bare key word only counts as a key when the sentence is talking about keys.
const KEY_PROSE = '(?:hold|press|hit|tap|with|then|or)\\s+';

// On a Mac a modifier is drawn, not spelled, mapped at RENDER time. Ctrl → ⌃, not ⌘: a "Ctrl"
// surviving to here is a literal Control key (the registry maps its own Ctrl bindings first).
const MAC_GLYPH = {
  Ctrl: '⌃', Control: '⌃', Alt: '⌥', Option: '⌥', Shift: '⇧',
  Cmd: '⌘', Command: '⌘', Meta: '⌘', Win: '⌘',
};
export const isMacPlatform = () => {
  if (typeof navigator === 'undefined') return false;   // Node (tests) — spell them out
  const p = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || '';
  return /mac/i.test(p || navigator.userAgent || '');
};

/** Whether `s` is a key combo and nothing else (what a trailing "(…)" must be to become keycaps). */
export const isKeyCombo = s =>
  new RegExp(`^(?:${CHAINED}|${GLYPHS}|${MOD}|${NAMED}|${KEYGLYPH})$`).test(String(s).trim());

// Render one combo (already validated by isKeyCombo) as keycaps: "Ctrl+Shift+Z" →
// three <kbd>s joined by "+"; the Mac glyph form "⇧⌘Z" → one <kbd> per glyph.
export const keysHtml = (combo, mac = isMacPlatform()) => {
  const s = String(combo).trim();
  const cap = k => `<kbd class="tip-key">${escapeHtml(mac && MAC_GLYPH[k] ? MAC_GLYPH[k] : k)}</kbd>`;
  const plus = '<span class="tip-plus">+</span>';
  // Apple prints ⇧⌘S with no joiner, but a run of bare glyphs reads as one symbol at a glance —
  // so every key is separated by "+", whichever form the combo arrived in.
  if (/^[⌃⌥⇧⌘]/.test(s)) {
    const glyphs = s.match(/^[⌃⌥⇧⌘]+/)[0].split('');
    const rest = s.slice(glyphs.length);
    return [...glyphs, ...(rest ? [rest] : [])].map(cap).join(plus);
  }
  return s.split('+').filter(Boolean).map(cap).join(plus);
};

// Combos written with "+" (or in Apple glyphs) are always highlighted; a lone "Shift"/"Enter"
// only when the prose says it is a key, or the app's own verbs would wear keycaps.
export const highlightKeys = (text, mac = isMacPlatform()) => {
  const re = new RegExp(`(${KEY_PROSE})?(${CHAINED}|${GLYPHS}|${MOD}|${NAMED})`, 'g');
  let out = '';
  let last = 0;
  for (const m of String(text).matchAll(re)) {
    const [full, lead, token] = m;
    const chained = new RegExp(`^(?:${CHAINED}|${GLYPHS})$`).test(token);
    if (!chained && !lead) continue;  // a bare key word with no key context — leave it alone
    out += escapeHtml(String(text).slice(last, m.index)) + escapeHtml(lead || '') + keysHtml(token, mac);
    last = m.index + full.length;
  }
  return out + escapeHtml(String(text).slice(last));
};
