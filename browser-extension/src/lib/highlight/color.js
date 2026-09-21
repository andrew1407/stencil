import { getSettings } from '../stencil.js';

// Mirrors the accent list in lib/accent.js — keep in sync (tests/accent.test.js pins it).
export const ACCENT_HEX = Object.freeze({
  violet: '#7c3aed', burgundy: '#660033', pink: '#ec4899', crimson: '#be123c',
  maroon: '#550000', orange: '#ea580c', brown: '#a87c50', yellow: '#eab308',
  grass: '#16a34a', green: '#047857', turquoise: '#40e0d0', aqua: '#0891b2',
  sky: '#0ea5e9', bluegray: '#7394b3', grey: '#64748b', blue: '#2563eb',
});
export const DEFAULT_HL = ACCENT_HEX.violet;
// Same string as the localStorage key in lib/accent.js.
export const ACCENT_STORAGE_KEY = 'stencil_accent';

// 'theme' (or empty) → the accent's hex; otherwise the setting IS the hex.
export const resolveHighlightColor = (setting, accentKey) =>
  (!setting || setting === 'theme') ? (ACCENT_HEX[accentKey] || DEFAULT_HL) : setting;

// Pass an already-loaded `settings` to skip the chrome.storage read.
export const highlightColorValue = async (settings) => {
  const { highlightColor } = settings || await getSettings();
  let accentKey = 'violet';
  try { accentKey = window.StencilAccent.get(); } catch { /* default */ }
  return resolveHighlightColor(highlightColor, accentKey);
};
