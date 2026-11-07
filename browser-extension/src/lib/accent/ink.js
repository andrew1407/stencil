// The accent's pure colour rules for the extension's modules: a hex normalised, and the ink that
// reads on it. Browser twin: js/core/settings/accents.js (tests/portParityFunctions.test.js);
// the pre-paint classic scripts keep their own copy in lib/prefs/prefs.js.
export const normalizeHex = (value) => {
  if (typeof value !== 'string') return null;
  let h = value.trim().replace(/^#/, '');
  if (/^[0-9a-fA-F]{3}$/.test(h)) h = h.split('').map((c) => c + c).join('');
  return /^[0-9a-fA-F]{6}$/.test(h) ? '#' + h.toLowerCase() : null;
};

const srgbToLinear = (c) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);

// WCAG relative luminance of a hex colour, or null when it isn't one.
export const relativeLuminance = (hex) => {
  const h = normalizeHex(hex);
  if (!h) return null;
  const [r, g, b] = [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16) / 255);
  return 0.2126 * srgbToLinear(r) + 0.7152 * srgbToLinear(g) + 0.0722 * srgbToLinear(b);
};

export const ON_ACCENT_LIGHT = '#ffffff';
export const ON_ACCENT_DARK = '#1a1a1a';

export const needsDarkGlyph = (hex) => {
  const l = relativeLuminance(hex);
  return l != null && (l + 0.05) / 0.05 > 1.05 / (l + 0.05);
};

export const onAccentInk = (hex) => (needsDarkGlyph(hex) ? ON_ACCENT_DARK : ON_ACCENT_LIGHT);
