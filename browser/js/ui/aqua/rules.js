// The aqua skin's table (config/aqua.json) and the pure rules over it: the wallpaper's cells and
// the word's lines, the glyphs borrowed from webcore.json. DOM-free; desktop twin:
// support/aqua/table.{hpp,cpp}, value for value.
import AQUA_DATA from '../../../../common/config/aqua.json' with { type: 'json' };
import { WEBCORE } from '../webcore/rules.js';
import { colorRuns, wordLinesOf } from '../skin/rules.js';
import { normalizeHex, onAccentInk } from '../../core/settings/accents.js';

export const AQUA = AQUA_DATA;
export const SKIN_NAME = 'aqua';
export const OFF_TOAST = AQUA.strings.off;
export const PROJECT_NAME = AQUA.strings.project;
export const IMAGE_NAME = AQUA.strings.imageName;

const channels = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
// `a` moved toward `b` by `share` in sRGB, each channel rounded: what CSS color-mix(in srgb) gives.
export const mixHex = (a, b, share) => {
  const [x, y] = [channels(a), channels(b)];
  return `#${x.map((c, i) => Math.round(c * (1 - share) + y[i] * share).toString(16).padStart(2, '0')).join('')}`;
};

// The gel tokens an accent paints (aqua.json `tint`): with the base hex they are the table's own.
export const gelTokens = (accent, dark = false) => {
  const tint = normalizeHex(accent) || AQUA.tint.base;
  const out = {};
  for (const [name, [toward, share]] of Object.entries(AQUA.tint[dark ? 'dark' : 'light'])) out[name] = mixHex(tint, toward, share);
  out['--aq-on-select'] = onAccentInk(out['--aq-select']);
  out['--aq-on-gel'] = onAccentInk(out['--aq-gel-bottom']);   // the word on a gel capsule, read against its deep half
  return out;
};

export const wallGrid = () => {
  const { width, height, cell } = AQUA.image;
  return { cell, cols: Math.floor(width / cell), rows: Math.floor(height / cell) };
};

export const rampAt = (cy, grid) => {
  const { ramp } = AQUA.image;
  return ramp[Math.min(ramp.length - 1, Math.floor((cy * ramp.length) / grid.rows))];
};

// Depth of (cx, cy) below swoosh s's crest, or -1 off its span; integer, so both painters agree.
export const swooshDepth = (s, cx, cy) => {
  const u = cx - s.x0;
  if (u < 0 || u > s.span) return -1;
  const top = s.base - Math.floor((s.amp * 4 * u * (s.span - u)) / (s.span * s.span));
  const d = cy - top;
  return d >= 0 && d < s.width ? d : -1;
};

export const wallColorAt = (cx, cy, grid) => {
  let color = rampAt(cy, grid);
  for (const s of AQUA.image.swooshes) {
    const d = swooshDepth(s, cx, cy);
    if (d < 0) continue;
    const rim = d < s.edge || d >= s.width - s.edge;
    if (!rim) color = s.color;
    else if ((cx + cy) & 1) color = s.edgeColor;
  }
  return color;
};

// Same-colour runs along each row of the wallpaper.
export const wallRuns = (grid) => colorRuns(grid, wallColorAt);

// The word as closed, locked, filled lines across the top of a w×h image, one gel colour each.
export const wordLines = (w, h) => wordLinesOf(w, h, WEBCORE.word, AQUA.word);
