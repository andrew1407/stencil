// The skin's icons (config/iconsWebcore.json): a palette map becomes <rect> runs on a 16-grid,
// installed into ui/icons.js so every glyph assembled afterwards wears it, and swapped into the
// ones already on the page. Desktop twin: support/webcore/icons.cpp.
import PIXELS from '../../../../common/config/iconsWebcore.json' with { type: 'json' };
import { setIconSkin } from '../icons.js';
import { swapGlyphs, swapLogoArt } from '../skin/glyphs.js';

// One <rect> per run of a lit colour along each row. `ink` re-inks single palette characters.
export const pixelRects = (rows, ink = null) => {
  let out = '';
  rows.forEach((row, y) => {
    for (let x = 0; x < row.length;) {
      const ch = row[x];
      let w = 1;
      while (x + w < row.length && row[x + w] === ch) w++;
      const fill = (ink && ink[ch]) || PIXELS.palette[ch];
      if (fill) out += `<rect x="${x}" y="${y}" width="${w}" height="1" fill="${fill}"/>`;
      x += w;
    }
  });
  return out;
};

// The dark face's own ink.
const themeInk = (dark) => (dark ? PIXELS.paletteDark : null);

// The mark's ring in the preset's own hex: the skin points --accent at its navy, so no var says it.
export const pixelLogoRects = (accent, dark = false) =>
  pixelRects(PIXELS.icons.logo, { ...themeInk(dark), ...(accent ? { [PIXELS.logoFrame]: accent } : null) });

const tables = { light: null, dark: null };
export const pixelTable = (dark = false) => {
  const key = dark ? 'dark' : 'light';
  return (tables[key] ??= Object.fromEntries(
    Object.entries(PIXELS.icons).map(([n, rows]) => [n, pixelRects(rows, themeInk(dark))])));
};

// One icon as a standalone document (the favicon); `accent` inks the mark's ring.
export const pixelIconSvg = (name, size = PIXELS.size, accent = null, dark = false) => {
  const art = (name === 'logo' && accent) ? pixelLogoRects(accent, dark) : (pixelTable(dark)[name] || '');
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="${size}" height="${size}" ` +
    `shape-rendering="crispEdges">${art}</svg>`;
};

export const setPixelIcons = (on, dark = false) => setIconSkin(on ? pixelTable(dark) : null);

const pixelMark = (accent, dark) => (logo) => {
  logo.innerHTML = pixelLogoRects(accent, dark);
  logo.setAttribute('viewBox', '0 0 16 16');
  logo.setAttribute('shape-rendering', 'crispEdges');
};

// The header mark alone: where every accent change lands while the skin is on.
export const paintLogoMark = (root, accent, dark = false) => swapLogoArt(root, true, pixelMark(accent, dark));

// The glyphs, and the header mark in the pixel art.
export const swapIconArt = (root, on, accent = null, dark = false) => {
  swapGlyphs(root, on);
  swapLogoArt(root, on, on ? pixelMark(accent, dark) : null);
};
