// The webcore skin's glyphs: iconsWebcore.json (a byte copy of browser/js/config/iconsWebcore.json,
// tests/dataParity.test.js) as <rect> runs on a 16-grid, swapped into every svg.ic, the header
// mark and the crop page's <img> mark. Browser twin: js/ui/webcore/icons.js.
import PIXELS from './iconsWebcore.json' with { type: 'json' };

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

const themeInk = (dark) => (dark ? PIXELS.paletteDark : null);

export const hasPixelIcon = (name) => Object.hasOwn(PIXELS.icons, name);

// `accent` inks the mark's ring (logoFrame); every other glyph ignores it.
export const pixelArt = (name, { accent = null, dark = false } = {}) => {
  const ink = { ...themeInk(dark), ...(name === 'logo' && accent ? { [PIXELS.logoFrame]: accent } : null) };
  return hasPixelIcon(name) ? pixelRects(PIXELS.icons[name], ink) : '';
};

export const pixelIconSvg = (name, opts = {}, size = PIXELS.size) =>
  `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="${size}" height="${size}" ` +
  `shape-rendering="crispEdges">${pixelArt(name, opts)}</svg>`;

const NAME_RE = /^ic-([a-z0-9-]+)$/;
const kept = new WeakMap();   // element → what it wore before the skin

const glyphOf = (el) => (el.classList.contains('logo') ? 'logo'
  : [...el.classList].map((c) => NAME_RE.exec(c)?.[1]).find((n) => n && hasPixelIcon(n)));

// An inline <svg> swaps its markup; the crop page's <img> mark swaps its source.
const WEARERS = Object.freeze({
  svg: {
    keep: (el) => ({ inner: el.innerHTML, viewBox: el.getAttribute('viewBox') }),
    wear: (el, name, opts) => {
      el.innerHTML = pixelArt(name, opts);
      el.setAttribute('viewBox', '0 0 16 16');
    },
    restore: (el, was) => {
      el.innerHTML = was.inner;
      if (was.viewBox) el.setAttribute('viewBox', was.viewBox);
    },
  },
  img: {
    keep: (el) => ({ src: el.getAttribute('src') }),
    wear: (el, name, opts) =>
      el.setAttribute('src', `data:image/svg+xml,${encodeURIComponent(pixelIconSvg(name, opts))}`),
    restore: (el, was) => el.setAttribute('src', was.src),
  },
});
const wearerOf = (el) => WEARERS[el.tagName.toLowerCase()] ?? null;

const paint = (el, name, opts) => {
  const wearer = wearerOf(el);
  if (!wearer) return;
  if (!kept.has(el)) kept.set(el, wearer.keep(el));
  wearer.wear(el, name, opts);
  el.setAttribute('data-wc', '');
};

const restore = (el) => {
  const was = kept.get(el), wearer = wearerOf(el);
  if (!was || !wearer) return;
  wearer.restore(el, was);
  el.removeAttribute('data-wc');
  kept.delete(el);
};

const GLYPHS = 'svg.ic, svg.logo, img.logo';

// Every glyph at or under `root`, drawn in pixels (`on`) or put back as it was.
export const swapIcons = (root, on, opts = {}) => {
  if (!root?.querySelectorAll) return;
  const els = [...(root.matches?.(GLYPHS) ? [root] : []), ...root.querySelectorAll(GLYPHS)];
  for (const el of els) {
    const name = glyphOf(el);
    if (!name) continue;
    if (on) paint(el, name, opts); else restore(el);
  }
};
