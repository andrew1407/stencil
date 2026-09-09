import ACCENTS_DATA from '../config/accents.json' with { type: 'json' };

// Accent (brand-colour) presets — theme-colour choices in the Visuals modal (🎨). First
// (violet) is the default. Each preset is just one primary hex; --accent-2 (hover-active
// shade) and the focus/selection glows derive from --accent via color-mix() in css/theme.css.
// The rows live in config/accents.json — the canonical palette the extension
// (extension/src/lib/accent.js) and desktop (theme.cpp accentPresets) mirror.
export const ACCENTS = ACCENTS_DATA;

// localStorage key for the chosen accent; same flavour as drawingApp_theme.
export const ACCENT_STORAGE_KEY = 'drawingApp_accent';
export const DEFAULT_ACCENT = 'violet';
export const isAccent = (key) => ACCENTS.some((a) => a.key === key);
// Primary hex for an accent key (falls back to the first preset — violet).
export const accentHex = (key) =>
  (ACCENTS.find((a) => a.key === key) || ACCENTS[0]).hex;

// The app favicon as an SVG string, with the panel outline painted in `hex` (the
// rest is fixed brand art). Mirrors favicon.svg — kept in sync by hand.
export const faviconSvg = (hex) =>
  '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">' +
  '<rect x="2" y="2" width="60" height="60" rx="13" fill="#2b2f3a"/>' +
  `<rect x="2.75" y="2.75" width="58.5" height="58.5" rx="12.25" fill="none" stroke="${hex}" stroke-width="1.5"/>` +
  '<rect x="12" y="12" width="40" height="40" rx="4" fill="#3a3f4b"/>' +
  '<polyline points="44,20 32,16 20,24 32,32 44,40 32,48 20,44" fill="none" stroke="#FFFF00" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>' +
  '<g fill="#FFFF00" stroke="#000000" stroke-width="1.25">' +
  '<circle cx="44" cy="20" r="2.6"/><circle cx="32" cy="16" r="2.6"/><circle cx="20" cy="24" r="2.6"/><circle cx="32" cy="32" r="2.6"/><circle cx="44" cy="40" r="2.6"/><circle cx="32" cy="48" r="2.6"/><circle cx="20" cy="44" r="2.6"/>' +
  '</g></svg>';

// Validate/normalize a hex colour: '#rgb' or '#rrggbb' (the leading '#' optional) →
// '#rrggbb' lower-case, or null when it isn't a hex. Used by the custom (non-preset)
// accent path — the logo double-click picker and `stencil.mainTheme = '#hex'`.
export const normalizeHex = (value) => {
  if (typeof value !== 'string') return null;
  let h = value.trim().replace(/^#/, '');
  if (/^[0-9a-fA-F]{3}$/.test(h)) h = h.split('').map((c) => c + c).join('');
  return /^[0-9a-fA-F]{6}$/.test(h) ? '#' + h.toLowerCase() : null;
};

// ── Accent contrast: which ink reads on the accent? ─────────────────────────
// Accent-backed controls paint their label and their currentColor line-art ON --accent,
// so the ink is whichever of white / near-black contrasts more (WCAG). Pure, so the
// extension (lib/accent.js) and desktop (theme.cpp) share the rule.
const srgbToLinear = (c) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);

// WCAG relative luminance of a hex colour, or null when it isn't one.
export const relativeLuminance = (hex) => {
  const h = normalizeHex(hex);
  if (!h) return null;
  const [r, g, b] = [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16) / 255);
  return 0.2126 * srgbToLinear(r) + 0.7152 * srgbToLinear(g) + 0.0722 * srgbToLinear(b);
};

// Contrast ratio of pure white / pure black against `hex` (1..21), null when not a hex.
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

// True when black reads better on `hex` than white does. A non-hex answers false.
export const needsDarkGlyph = (hex) => {
  const l = relativeLuminance(hex);
  return l != null && (l + 0.05) / 0.05 > 1.05 / (l + 0.05);
};

// The ink to paint on `hex`.
export const onAccentInk = (hex) => (needsDarkGlyph(hex) ? ON_ACCENT_DARK : ON_ACCENT_LIGHT);

// The presets that take the dark ink, DERIVED from the palette. Mirrored by
// prePaintTheme.js (which can't import this module) and asserted equal in the tests.
export const LIGHT_ACCENT_KEYS = ACCENTS.filter((a) => needsDarkGlyph(a.hex)).map((a) => a.key);

// Normalize any CSS color ('red', rgb()/hsl(), #rgb) to '#rrggbb' via a canvas probe,
// since <input type=color> only takes #rrggbb. 'transparent'/null pass through; an
// unparseable value returns unchanged. Needs a DOM canvas — passes through in node tests.
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
  // The browser resolves any valid CSS color via fillStyle; probe two bases so an
  // INVALID value (which leaves each base untouched) is detected and left as-is.
  colorCanvas.fillStyle = '#000'; colorCanvas.fillStyle = s; const a = colorCanvas.fillStyle;
  colorCanvas.fillStyle = '#fff'; colorCanvas.fillStyle = s; const b = colorCanvas.fillStyle;
  if (a !== b) return s;                       // unparseable → unchanged
  if (/^#[0-9a-f]{6}$/i.test(a)) return a;
  const m = /^rgba?\(\s*(\d+),\s*(\d+),\s*(\d+)/i.exec(a);   // alpha form → drop alpha
  return m ? '#' + [m[1], m[2], m[3]].map((n) => (+n).toString(16).padStart(2, '0')).join('') : s;
};

// Repaint the browser-tab favicon (and the PWA status-bar theme-color) to a literal hex.
// The favicon is a static .svg file the browser can't read our CSS var from, so we swap
// the <link> to an inline data-URL SVG carrying the colour.
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

// Repaint the favicon to a preset accent key (falls back to violet for an unknown key).
export const applyAccentFavicon = (key) =>
  applyFaviconHex(accentHex(isAccent(key) ? key : DEFAULT_ACCENT));
