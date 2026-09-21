// ── A written shortcut vs a keystroke ────────────────────────────────────────
// Byte-pinned PORT of browser/js/ui/control/comboMatch.js: a written combo vs a KeyboardEvent. Pure.

// A shortcut is written to be READ ("⇧⌘S", "Alt+0"), so both sides are reduced to the same
// shape — modifiers plus one key — before comparing. Pure, so it is tested without a keyboard.
const MOD_OF = {
  '⌃': 'Ctrl', '⌥': 'Alt', '⇧': 'Shift', '⌘': 'Meta',
  CTRL: 'Ctrl', CONTROL: 'Ctrl', ALT: 'Alt', OPTION: 'Alt', SHIFT: 'Shift',
  META: 'Meta', CMD: 'Meta', COMMAND: 'Meta', WIN: 'Meta',
};
const KEY_OF = { ESC: 'ESCAPE', DEL: 'DELETE', RETURN: 'ENTER', ' ': 'SPACE' };
const keyName = (k) => {
  const u = String(k == null ? '' : k).toUpperCase();
  return KEY_OF[u] || u;
};

export const parseCombo = (combo) => {
  const s = String(combo == null ? '' : combo).trim();
  // Apple's glyph form carries no joiner ("⇧⌘S"); every other form is "+"-separated.
  const lead = s.match(/^[⌃⌥⇧⌘]+/);
  const tokens = lead ? [...lead[0], s.slice(lead[0].length)] : s.split('+');
  const mods = new Set();
  let key = '';
  for (const raw of tokens) {
    const t = raw.trim();
    if (!t) continue;
    const mod = MOD_OF[t] || MOD_OF[t.toUpperCase()];
    if (mod) mods.add(mod);
    else key = keyName(t);
  }
  return { mods, key };
};

// What was actually pressed. Both `key` and the PHYSICAL `code` are kept: on a Mac
// Alt+A reports key "å", so the code is the only side that still says "A".
export const eventCombo = (e) => {
  const mods = new Set();
  if (e.ctrlKey) mods.add('Ctrl');
  if (e.altKey) mods.add('Alt');
  if (e.shiftKey) mods.add('Shift');
  if (e.metaKey) mods.add('Meta');
  const code = String(e.code || '').replace(/^(?:Key|Digit|Numpad)/, '');
  return { mods, keys: [keyName(e.key), code ? keyName(code) : ''].filter(Boolean) };
};

export const comboMatchesEvent = (combo, e) => {
  const want = parseCombo(combo);
  // Nothing a keystroke can be: a modifier-only combo leaves no key at all, and a
  // gesture the caps spell out ("Alt+click", "Shift+left-drag") leaves a word instead.
  if (!want.key) return false;
  const got = eventCombo(e);
  if (want.mods.size !== got.mods.size) return false;
  for (const m of want.mods) if (!got.mods.has(m)) return false;
  return got.keys.includes(want.key);
};
