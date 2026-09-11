import { core } from '../core/stencilCore.js';
// ── CSS colours, with alpha ─────────────────────────────────────
// Colours are stored as CSS, written straight into a canvas context: `#rrggbb` opaque,
// `#rrggbbaa` translucent. <input type="color"> has no alpha byte, so the editors keep it
// in a separate opacity control and these two join the halves. core's parseHex checks
// `< 7`, so the CLI and pystencil read the RGB and ignore the alpha.
// Desktop twin: support/cssColor.hpp — keep the pair in step.

// Split a stored colour into the swatch's own 7-char hex and an opacity in 0..1. Anything
// that is not a hex colour (a CSS name, 'transparent') keeps its value and reads as opaque,
// so a control fed one still shows something sensible. Pure.
export const cssColorParts = (value) => {
  const v = typeof value === 'string' ? value.trim() : '';
  if (/^#[0-9a-fA-F]{8}$/.test(v))
    return { hex: v.slice(0, 7).toLowerCase(), alpha: parseInt(v.slice(7), 16) / 255 };
  if (/^#[0-9a-fA-F]{6}$/.test(v)) return { hex: v.toLowerCase(), alpha: 1 };
  return { hex: v, alpha: 1 };
};

// …and back. Fully opaque writes the plain `#rrggbb` every surface reads; only a real
// alpha adds the byte. Pure.
export const cssWithAlpha = (hex, alpha) => {
  const h = typeof hex === 'string' ? hex.trim().toLowerCase() : '';
  const a = Math.max(0, Math.min(1, Number(alpha)));
  if (!/^#[0-9a-f]{6}$/.test(h) || !Number.isFinite(a)) return hex;
  if (a >= 1) return h;
  return h + Math.round(a * 255).toString(16).padStart(2, '0');
};

// ── A colour control and its opacity box ────────────────────────────────────
// <input type="color"> cannot carry an alpha byte, so every editable colour in the app is
// TWO controls — a swatch and a 0-255 opacity box — that these two put together and take
// apart. One seam, so the selection panel, the toolbar binder and the fullscreen mirror
// all read and write the pair the same way.

// An opacity box's 0-255 byte as the 0..1 fraction cssWithAlpha wants. A blank or
// out-of-range box reads as fully opaque rather than making the line vanish. Pure.
const alphaFraction = (input) => {
  const n = Number(input?.value);
  if (!Number.isFinite(n)) return 1;
  return Math.max(0, Math.min(255, n)) / 255;
};

// The pair as one CSS colour: `#rrggbb` opaque, `#rrggbbaa` otherwise.
export const readColorPair = (colorId, alphaId, doc = document) =>
  cssWithAlpha(doc.getElementById(colorId).value, alphaFraction(doc.getElementById(alphaId)));

// …and the way back: a stored colour split across the swatch and the opacity box.
export const writeColorPair = (colorId, alphaId, value, doc = document) => {
  const { hex, alpha } = cssColorParts(value);
  const swatch = doc.getElementById(colorId);
  if (swatch) swatch.value = hex;
  const box = doc.getElementById(alphaId);
  if (box) box.value = String(Math.round(alpha * 255));
};

// An area's fill is its swatch plus its alpha, and all the way down at 0 IS "no fill" —
// stored as NO_FILL, the value every surface reads as unfilled.
export const NO_FILL = 'transparent';
export const fillFromPair = (colorId, alphaId, doc = document) =>
  alphaFraction(doc.getElementById(alphaId)) <= 0 ? NO_FILL : readColorPair(colorId, alphaId, doc);

// ── Color helpers (pure) ────────────────────────────────────────
// "#rrggbb" + alpha → "rgba(...)"; already-rgba/named values pass through unchanged.
export const hexToRgba = (hex, alpha) => {
  if (typeof hex !== 'string' || hex[0] !== '#' || hex.length < 7) return hex;
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  return `rgba(${r},${g},${b},${alpha})`;
};

// Parse "#rrggbb" → { r, g, b }. Delegates to the shared C++ core (wasm) when loaded
// and the string is a valid 7-char hex; the JS body is reference + fallback.
export const parseHex = hex => {
  const fn = core.op('parseHex');
  if (fn) {
    const rgb = fn(hex);
    if (rgb) return rgb;
  }
  return {
    r: parseInt(hex.slice(1, 3), 16),
    g: parseInt(hex.slice(3, 5), 16),
    b: parseInt(hex.slice(5, 7), 16),
  };
};
