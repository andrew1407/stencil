// ── Theme for the INJECTED in-page modal shell ──────────────────────────────
// lib/overlay.js mounts the crop / editor modal into an arbitrary web page, so it
// can't link lib/theme.css or read the extension's CSS variables — it gets its palette
// handed to it as DATA. This module is that hand-off: the same Appearance choice and
// accent every other surface follows (lib/accent.js writes them to localStorage and
// mirrors both into chrome.storage.local, which is readable from a page OR the service
// worker, the two places a modal is launched from).
//
// The MODE travels unresolved ('system' included): only the target page can answer
// what the OS prefers, so the injected shell resolves it with its own matchMedia and
// re-resolves if the choice changes while the modal is open.
import { ACCENT_HEX, DEFAULT_HL, ACCENT_STORAGE_KEY } from './highlightColor.js';

// chrome.storage.local key the Appearance mode is mirrored under (same string as the
// localStorage key in lib/accent.js, so the two never drift).
export const THEME_STORAGE_KEY = 'stencil_theme';
export const THEME_MODES = ['system', 'light', 'dark'];

// The two palettes, lifted verbatim from lib/theme.css — the shell must look like the
// extension's own chrome, and the framed page inside it uses exactly these values.
export const SHELL_PALETTES = {
  dark: { bg: '#21242d', panel: '#2b2f3a', panel2: '#343948', line: '#3d4354', text: '#e8eaf0', muted: '#9aa0b0' },
  light: { bg: '#f4f5f7', panel: '#ffffff', panel2: '#eceef3', line: '#d4d8e2', text: '#1d2230', muted: '#6b7180' },
};

/** 'system' resolves against the target's OS preference; anything else is taken as-is. */
export const resolveShellMode = (mode, prefersDark = false) =>
  (mode === 'dark' || mode === 'light') ? mode : (prefersDark ? 'dark' : 'light');

/** The palette for a (possibly unresolved) mode. */
export const shellPalette = (mode, prefersDark = false) => SHELL_PALETTES[resolveShellMode(mode, prefersDark)];

/** Accent KEY → hex, falling back to the default accent. */
export const shellAccent = (accentKey) => ACCENT_HEX[accentKey] || DEFAULT_HL;

/**
 * The theme payload handed to mountStencilModal. Readable from a page and from the
 * service worker (chrome.storage.local), and JSON-serializable — executeScript args
 * are structured-cloned. Carries the palettes themselves so the injected shell needs
 * no copy of them, and the accent MAP so it can re-resolve a live accent change.
 * @returns {Promise<{mode: string, accent: string, palettes: object, accents: object}>}
 */
export const loadShellTheme = async () => {
  let mode = 'system';
  let accentKey = 'violet';
  try {
    const s = await chrome.storage.local.get([THEME_STORAGE_KEY, ACCENT_STORAGE_KEY]);
    if (THEME_MODES.includes(s[THEME_STORAGE_KEY])) mode = s[THEME_STORAGE_KEY];
    if (typeof s[ACCENT_STORAGE_KEY] === 'string' && s[ACCENT_STORAGE_KEY]) accentKey = s[ACCENT_STORAGE_KEY];
  } catch {
    /* never mirrored yet (fresh profile) → the defaults above */
  }
  return { mode, accent: shellAccent(accentKey), palettes: SHELL_PALETTES, accents: ACCENT_HEX };
};
