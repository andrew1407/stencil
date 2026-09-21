import ACCENTS_DATA from '../../config/accents.json' with { type: 'json' };
import SVG_ART from '../../config/svgArt.json' with { type: 'json' };

// Accent presets: one primary hex each; --accent-2 and the glows derive via color-mix() in
// css/theme.css. config/accents.json is canonical — the extension (lib/accent.js) and
// desktop (theme.cpp accentPresets) mirror it.
export const ACCENTS = ACCENTS_DATA;

export const ACCENT_STORAGE_KEY = 'drawingApp_accent';
export const DEFAULT_ACCENT = 'violet';
export const isAccent = (key) => ACCENTS.some((a) => a.key === key);
export const accentHex = (key) =>
  (ACCENTS.find((a) => a.key === key) || ACCENTS[0]).hex;

// The favicon art lives in config/svgArt.json, pinned against favicon.svg by tests/svgArt.test.js.
export const faviconSvg = (hex) => SVG_ART.favicon.replace('%1', hex);

// '#rgb' / '#rrggbb' (leading '#' optional) → '#rrggbb' lower-case, or null.
export const normalizeHex = (value) => {
  if (typeof value !== 'string') return null;
  let h = value.trim().replace(/^#/, '');
  if (/^[0-9a-fA-F]{3}$/.test(h)) h = h.split('').map((c) => c + c).join('');
  return /^[0-9a-fA-F]{6}$/.test(h) ? '#' + h.toLowerCase() : null;
};

// Ink on --accent is whichever of white / near-black contrasts more (WCAG). Pure, so the
// extension (lib/accent.js) and desktop (theme.cpp) share the rule.
const srgbToLinear = (c) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);

// WCAG relative luminance of a hex colour, or null when it isn't one.
export const relativeLuminance = (hex) => {
  const h = normalizeHex(hex);
  if (!h) return null;
  const [r, g, b] = [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16) / 255);
  return 0.2126 * srgbToLinear(r) + 0.7152 * srgbToLinear(g) + 0.0722 * srgbToLinear(b);
};

// 1..21, null when not a hex.
export const contrastWithWhite = (hex) => {
  const l = relativeLuminance(hex);
  return l == null ? null : 1.05 / (l + 0.05);
};
export const contrastWithBlack = (hex) => {
  const l = relativeLuminance(hex);
  return l == null ? null : (l + 0.05) / 0.05;
};

// The dark ink is the page ink, not pure black, so it matches the app's other glyphs.
export const ON_ACCENT_LIGHT = '#ffffff';
export const ON_ACCENT_DARK = '#1a1a1a';

export const needsDarkGlyph = (hex) => {
  const l = relativeLuminance(hex);
  return l != null && (l + 0.05) / 0.05 > 1.05 / (l + 0.05);
};

export const onAccentInk = (hex) => (needsDarkGlyph(hex) ? ON_ACCENT_DARK : ON_ACCENT_LIGHT);

// Derived from the palette; mirrored by prePaintTheme.js (which can't import this module).
export const LIGHT_ACCENT_KEYS = ACCENTS.filter((a) => needsDarkGlyph(a.hex)).map((a) => a.key);

// Any CSS color → '#rrggbb' via a canvas probe (<input type=color> only takes #rrggbb).
// 'transparent'/null pass through; unparseable returns unchanged; no canvas in node.
const colorCanvas = (() => {
  try { return document.createElement('canvas').getContext('2d'); } catch { return null; }
})();
export const toHexColor = (v) => {
  if (v == null) return v;
  const s = String(v).trim();
  if (!s || s.toLowerCase() === 'transparent') return s;
  if (/^#[0-9a-f]{6}$/i.test(s)) return s.toLowerCase();
  if (/^#[0-9a-f]{3}$/i.test(s)) return ('#' + s.slice(1).replace(/./g, (c) => c + c)).toLowerCase();
  if (!colorCanvas) return s;
// Probe two bases: an invalid value leaves each base untouched.
  colorCanvas.fillStyle = '#000'; colorCanvas.fillStyle = s; const a = colorCanvas.fillStyle;
  colorCanvas.fillStyle = '#fff'; colorCanvas.fillStyle = s; const b = colorCanvas.fillStyle;
  if (a !== b) return s;
  if (/^#[0-9a-f]{6}$/i.test(a)) return a;
  const m = /^rgba?\(\s*(\d+),\s*(\d+),\s*(\d+)/i.exec(a);   // alpha form → drop alpha
  return m ? '#' + [m[1], m[2], m[3]].map((n) => (+n).toString(16).padStart(2, '0')).join('') : s;
};

// The favicon is a static .svg that can't read a CSS var, so the <link> becomes an inline data URL.
export const applyFaviconHex = (hex) => {
  if (typeof document === 'undefined') return;
  let link = document.querySelector('link[rel="icon"]');
  if (!link) {
    link = document.createElement('link');
    link.rel = 'icon';
    document.head.appendChild(link);
  }
  link.type = 'image/svg+xml';
  link.href = 'data:image/svg+xml,' + encodeURIComponent(faviconSvg(hex));
  const meta = document.querySelector('meta[name="theme-color"]');
  if (meta) meta.setAttribute('content', hex);
};

export const applyAccentFavicon = (key) =>
  applyFaviconHex(accentHex(isAccent(key) ? key : DEFAULT_ACCENT));
