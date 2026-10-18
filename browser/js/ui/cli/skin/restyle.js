// Every run in the accent re-dressed in the skin that is on, the DOM port of
// cli/src/console/render/ansi/restyle.zig: the picture skins turn each accent letter into a
// picture two cells wide and the rest of the accent gold, the rainbow and the bifrost colour
// each accent cell by its column, the matrix and the blue screen rewrite every colour to their own.
import { traitsOf, hueAt, GOLD } from './table.js';
import { pictureAt, picture } from './pictures.js';

export const PICTURE_CLASS = 'cli-picture';
// The hues repeat every 180 columns: 180 × 22° is a whole number of turns.
export const HUE_PERIOD = 180;

// Whether the skin re-dresses the rows at all; the fairy lights recolour the accent itself.
export const restyles = (skin) => {
  const t = traitsOf(skin);
  return !!t && !!(t.paintsCells || t.paintsRuns || t.replacesLetters);
};

// What the theme accent paints as under a skin: a cell painter's fg, a picture skin's gold, else itself.
export const accentUnder = (skin, accent) => {
  const t = traitsOf(skin);
  if (t?.paintsCells) return t.fg;
  if (t?.replacesLetters) return GOLD;
  return accent;
};

// A run-painting skin's hues as one repeating gradient, a hard stop per column; the bifrost
// slides it a column a frame through --cli-slide.
export const hueGradient = (skin) => {
  const stops = [];
  for (let c = 0; c < HUE_PERIOD; c++) stops.push(`${hueAt(skin, c, 0)} ${c}ch ${c + 1}ch`);
  return `repeating-linear-gradient(90deg, ${stops.join(', ')})`;
};

// FNV-1a of a row's text: the key its pictures hash on (Wyhash in the CLI).
export const rowKey = (text) => {
  let h = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) { h ^= text.charCodeAt(i) & 0xff; h = Math.imul(h, 0x01000193) >>> 0; }
  return h >>> 0;
};

// An accent letter becomes a picture: printable ASCII, not a space.
export const swappable = (ch) => ch > ' ' && ch < '\x7f';

// `el` filled with `text`, an accent run starting at cell column `col0`: under a picture skin
// each letter a picture two cells wide (the CLI places the cursor after each), else the text.
export const fillRun = (el, text, { skin = null, clock = null, key = 0, col0 = 0 } = {}) => {
  while (el.firstChild) el.removeChild(el.firstChild);
  const t = traitsOf(skin);
  if (!t?.replacesLetters || !clock) { el.textContent = text; return; }
  const doc = el.ownerDocument || globalThis.document;
  let col = col0, prev = null, plain = '';
  const flush = () => { if (plain) { el.appendChild(doc.createTextNode(plain)); plain = ''; } };
  for (const ch of text) {
    if (!swappable(ch)) { plain += ch; col += 1; continue; }
    flush();
    const k = pictureAt(skin, key, col, prev, clock);
    prev = k;
    const s = doc.createElement('span');
    s.className = PICTURE_CLASS;
    s.textContent = picture(skin, k);
    el.appendChild(s);
    col += 2;
  }
  flush();
};
