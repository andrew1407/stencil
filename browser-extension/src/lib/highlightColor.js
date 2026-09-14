import { getSettings } from './stencil.js';

// Mirrors the accent list in lib/accent.js — keep in sync (tests/accent.test.js pins it).
export const ACCENT_HEX = Object.freeze({
  violet: '#7c3aed', pink: '#ec4899', yellow: '#eab308', orange: '#ea580c',
  crimson: '#be123c', aqua: '#0891b2', sky: '#0ea5e9', blue: '#2563eb',
  grass: '#16a34a', green: '#047857', brown: '#a87c50', grey: '#64748b',
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
