// ── Highlight colour resolution ──────────────────────────────────────────────
import { getSettings } from './stencil.js';

// The on-page highlight outline can follow the main accent ('theme') or use a custom
// hex. The accent KEY lives in localStorage (lib/accent.js) and is mirrored to
// chrome.storage.local so non-page contexts (service worker, the page-API bridge) can
// resolve it. ACCENT_HEX mirrors the accent list in lib/accent.js — keep in sync.
export const ACCENT_HEX = {
  violet: '#7c3aed', pink: '#ec4899', yellow: '#eab308', orange: '#ea580c',
  crimson: '#be123c', aqua: '#0891b2', sky: '#0ea5e9', blue: '#2563eb',
  grass: '#16a34a', green: '#047857', brown: '#a87c50', grey: '#64748b',
};
export const DEFAULT_HL = ACCENT_HEX.violet;
// chrome.storage.local key the accent choice is mirrored under (same string as the
// localStorage key in lib/accent.js, so the two never drift).
export const ACCENT_STORAGE_KEY = 'stencil_accent';

// setting === 'theme' (or empty) → the accent's hex; otherwise the setting IS the hex.
export const resolveHighlightColor = (setting, accentKey) =>
  (!setting || setting === 'theme') ? (ACCENT_HEX[accentKey] || DEFAULT_HL) : setting;

// The effective highlight hex RIGHT NOW: the stored highlightColor setting resolved
// against this page's accent (window.StencilAccent; default outside a page). Shared
// by the popup and the chat page. Pass an already-loaded `settings` object to skip
// the chrome.storage read.
export const highlightColorValue = async (settings) => {
  const { highlightColor } = settings || await getSettings();
  let accentKey = 'violet';
  try { accentKey = window.StencilAccent.get(); } catch { /* default */ }
  return resolveHighlightColor(highlightColor, accentKey);
};
