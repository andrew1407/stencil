// The skin's icons (config/iconsWebcore.json): a palette map becomes <rect> runs on a 16-grid,
// installed into ui/icons.js so every glyph assembled afterwards wears it, and swapped into the
// ones already on the page. Desktop twin: support/webcore/icons.cpp.
import PIXELS from '../../config/iconsWebcore.json' with { type: 'json' };
import { icon, setIconSkin, DRAW_MODE_ICON } from '../icons.js';

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

const themeInk = (dark) => (dark ? PIXELS.paletteDark : null);   // the dark face's own ink

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

const NAME_RE = /^ic-([a-z0-9-]+)$/;
const SW_RE = /^ic-sw([\d.]+)$/;
let logoArt = null;   // the header mark's own markup and grid, kept to put back

// The header mark alone: where every accent change lands while the skin is on.
export const paintLogoMark = (root, accent, dark = false) => {
  const logo = root?.querySelector?.('svg.app-logo');
  if (logo && logo.getAttribute('viewBox') === '0 0 16 16') logo.innerHTML = pixelLogoRects(accent, dark);
};

// Redraw every glyph under `root` in whatever ui/icons.js is serving now; the header mark and the
// draw-mode faces follow, in place (their listeners stay).
export const swapIconArt = (root, on, accent = null, dark = false) => {
  for (const svg of [...root.querySelectorAll('svg.ic')]) {
    if (svg.classList.contains('draw-mode-icon')) continue;   // not in the line-art table
    const classes = [...svg.classList];
    const name = classes.map((c) => NAME_RE.exec(c)?.[1]).find(Boolean);
    if (!name) continue;
    // The line-art's stroke width rides along as a class, so the way back keeps it.
    const stroke = svg.getAttribute('stroke-width');
    const kept = classes.filter((c) => c !== 'ic' && c !== `ic-${name}` && !SW_RE.test(c));
    if (on && stroke && stroke !== '2') kept.push(`ic-sw${stroke}`);
    const sw = classes.map((c) => SW_RE.exec(c)?.[1]).find(Boolean);
    svg.outerHTML = icon(name, { size: Number(svg.getAttribute('width')) || 16, cls: kept.join(' '), sw: sw ? Number(sw) : 2 });
  }
  for (const svg of [...root.querySelectorAll('svg.draw-mode-icon')]) {
    const rect = svg.classList.contains('ic-draw-mode-rect') || /ic-box/.test(svg.innerHTML);
    svg.outerHTML = rect ? DRAW_MODE_ICON.rect : DRAW_MODE_ICON.line;
  }
  // The install button is one big arrow of its own (see iconsWebcore.json `install`).
  const install = on ? root.querySelector?.('#install-toggle svg.ic') : null;
  if (install) install.innerHTML = pixelTable(dark).install;
  const logo = root.querySelector('svg.app-logo');
  if (!logo) return;
  if (on) {
    logoArt ??= { inner: logo.innerHTML, viewBox: logo.getAttribute('viewBox') };
    logo.innerHTML = pixelLogoRects(accent, dark);
    logo.setAttribute('viewBox', '0 0 16 16');
    logo.setAttribute('shape-rendering', 'crispEdges');
  } else if (logoArt) {
    logo.innerHTML = logoArt.inner;
    logo.setAttribute('viewBox', logoArt.viewBox);
    logo.removeAttribute('shape-rendering');
  }
};
