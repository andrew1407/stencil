// The injected modal shell (lib/overlay.js) cannot link lib/theme/, so its palette travels
// as data. The MODE travels unresolved: only the target page can answer what the OS prefers.
import { ACCENT_HEX, DEFAULT_HL, ACCENT_STORAGE_KEY } from './highlightColor.js';

// Same string as the localStorage key in lib/accent.js.
export const THEME_STORAGE_KEY = 'stencil_theme';
export const THEME_MODES = Object.freeze(['system', 'light', 'dark']);

// Lifted verbatim from lib/theme/palette.css.
export const SHELL_PALETTES = Object.freeze({
  dark: { bg: '#21242d', panel: '#2b2f3a', panel2: '#343948', line: '#3d4354', text: '#e8eaf0', muted: '#9aa0b0' },
  light: { bg: '#f4f5f7', panel: '#ffffff', panel2: '#eceef3', line: '#d4d8e2', text: '#1d2230', muted: '#6b7180' },
});

export const resolveShellMode = (mode, prefersDark = false) =>
  (mode === 'dark' || mode === 'light') ? mode : (prefersDark ? 'dark' : 'light');

export const shellPalette = (mode, prefersDark = false) => SHELL_PALETTES[resolveShellMode(mode, prefersDark)];

export const shellAccent = (accentKey) => ACCENT_HEX[accentKey] || DEFAULT_HL;

// The payload handed to mountStencilModal — JSON-serializable, since executeScript args
// are structured-cloned; it carries the accent map so the shell can re-resolve a live change.
export const loadShellTheme = async () => {
  let mode = 'system';
  let accentKey = 'violet';
  try {
    const s = await chrome.storage.local.get([THEME_STORAGE_KEY, ACCENT_STORAGE_KEY]);
    if (THEME_MODES.includes(s[THEME_STORAGE_KEY])) mode = s[THEME_STORAGE_KEY];
    if (typeof s[ACCENT_STORAGE_KEY] === 'string' && s[ACCENT_STORAGE_KEY]) accentKey = s[ACCENT_STORAGE_KEY];
  } catch {
    /* never mirrored yet */
  }
  return { mode, accent: shellAccent(accentKey), palettes: SHELL_PALETTES, accents: ACCENT_HEX };
};
