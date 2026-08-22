import { icon } from '../ui/icons.js';
import { themeSwap, originOf, originOfId } from '../ui/motion.js';
import { ACCENT_STORAGE_KEY, DEFAULT_ACCENT, isAccent, applyAccentFavicon, applyFaviconHex, normalizeHex, accentHex, needsGlyphShadow } from './accents.js';

// ── Appearance mode ────────────────────────────────────────────────
// Three states, like the desktop and the extension: 'system' follows the OS and is the
// DEFAULT, 'light'/'dark' pin the palette. Stored under the same key the pre-paint script
// reads (js/prePaintTheme.js), which resolves 'system' the same way before first paint.
export const THEME_STORAGE_KEY = 'drawingApp_theme';
export const THEME_MODES = ['system', 'light', 'dark'];

// Resolve a mode to the palette to paint. 'system' is answered by the OS at call time
// (never stored resolved) — that is what lets the app follow a later OS change.
export const resolveThemeMode = (mode, prefersDark = typeof matchMedia === 'function'
  && matchMedia('(prefers-color-scheme: dark)').matches) =>
  (mode === 'dark' || mode === 'light') ? mode : (prefersDark ? 'dark' : 'light');

// Wipe origin for a palette change: the theme button first, then the other toolbar icons
// that lead to the same settings — whichever is actually ON SCREEN. Null (toolbar hidden)
// makes themeSwap fall back to the viewport centre rather than a stale click.
const themeOrigin = () => originOfId('theme-toggle') || originOfId('visuals-btn') || originOfId('settings-btn');
const accentOrigin = () => originOfId('visuals-btn') || originOfId('theme-toggle') || originOfId('settings-btn');

// ── AccentController: UI theme + accent preset/custom colour ────────
// Holds no state — theme/accent live on the document element (data-theme / data-accent /
// inline --accent): the app keeps the getters that read them, this controller does the
// writes. Back-references the app for the theme icon and the cross-tab broadcast.
export class AccentController {
  constructor(app) {
    this.app = app;
  }

  setTheme(theme, originEl = null) {
    this.setThemeMode(String(theme).toLowerCase() === 'dark' ? 'dark' : 'light', originEl);
  }

  // Set the APPEARANCE mode — 'system' (follow the OS, the default), 'light' or 'dark'.
  // Same tri-state as the desktop (io/fileStore.hpp themeMode) and the extension
  // (lib/shellTheme.js THEME_MODES). `originEl` is the control the wipe floods out of.
  setThemeMode(mode, originEl = null) {
    const next = THEME_MODES.includes(mode) ? mode : 'system';
    // Only a PALETTE change is worth animating. Moving between two modes that resolve to
    // the same palette — System while the OS is dark, then Dark — repaints nothing, so the
    // wipe would play over an unchanged screen. Store the mode and stay still.
    const painted = document.documentElement.getAttribute('data-theme');
    if (resolveThemeMode(next) === painted) {
      localStorage.setItem(THEME_STORAGE_KEY, next);
      this.updateThemeIcon();
      try { window.dispatchEvent(new CustomEvent('stencil:theme-changed', { detail: next })); } catch { /* no DOM */ }
      return;
    }
    themeSwap(() => {
      // 'system' is stored as itself and RESOLVED for painting, so a later OS change is
      // still followed (controlsBinder listens on the media query for exactly this).
      document.documentElement.setAttribute('data-theme', resolveThemeMode(next));
      localStorage.setItem(THEME_STORAGE_KEY, next);
      this.updateThemeIcon();
      // Anything showing the MODE (the Appearance row in Default Visuals) has to hear it:
      // the toolbar's toggle moves the same setting from the other side.
      try { window.dispatchEvent(new CustomEvent('stencil:theme-changed', { detail: next })); } catch { /* no DOM — best-effort UI nudge */ }
    }, () => originOf(originEl) || themeOrigin());
  }

  /** The stored appearance mode ('system' when unset or unreadable). */
  get themeMode() {
    try {
      const v = localStorage.getItem(THEME_STORAGE_KEY);
      return THEME_MODES.includes(v) ? v : 'system';
    } catch {
      return 'system';   // storage blocked (private mode) — the default stands
    }
  }

  // EVERY copy of the button, not getElementById's first hit: the fullscreen layer clones
  // the toolbar with its ids, so the hidden clone would win the lookup and take the new
  // icon. Same trap originOfId (ui/motion.js) sidesteps for the wipe's origin.
  updateThemeIcon() {
    const glyph = this.app.theme === 'dark' ? icon('sun') : icon('moon');
    for (const btn of document.querySelectorAll('[id="theme-toggle"]')) btn.innerHTML = glyph;
  }

  // Flag a LIGHT accent on <html> so the white glyphs pick up their dark shadow
  // (css/theme.css --glyph-shadow). Driven by the resolved hex, so presets and
  // custom colours go through the same test. Mirrors prePaintTheme.js's pre-paint pass.
  applyGlyphContrast(hex) {
    document.documentElement.toggleAttribute('data-accent-light', needsGlyphShadow(hex));
  }

  setAccent(key, originEl = null) {
    const next = this.applyAccent(key, originEl);
    this.app.tabs.broadcastAccent(next);
  }

  // Paint + persist the accent in THIS tab; returns the resolved key. Used by setAccent
  // (local change) and the cross-tab listener (remote change, no re-broadcast).
  applyAccent(key, originEl = null) {
    const next = isAccent(key) ? key : DEFAULT_ACCENT;
    // Same flood as the theme swap — an accent change repaints as much of the page.
    themeSwap(() => {
      document.documentElement.style.removeProperty('--accent'); // drop any custom (temp) override
      document.documentElement.setAttribute('data-accent', next);
      try { localStorage.setItem(ACCENT_STORAGE_KEY, next); } catch { /* storage blocked — accent still applies this session, just won't persist */ }
      applyAccentFavicon(next);
      this.applyGlyphContrast(accentHex(next));
    }, () => originOf(originEl) || accentOrigin());
    return next;
  }

  // Page-only accent: NO persistence and NO cross-tab broadcast (unlike setAccent) — it
  // vanishes on reload. The inline --accent overrides the data-accent preset rule.
  // Returns the normalized '#rrggbb', or null when `hex` isn't a valid colour.
  setCustomAccent(hex, originEl = null) {
    const norm = normalizeHex(hex);
    if (!norm) return null;
    themeSwap(() => {
      document.documentElement.style.setProperty('--accent', norm);
      applyFaviconHex(norm);
      this.applyGlyphContrast(norm);
    }, () => originOf(originEl) || accentOrigin());
    return norm;
  }
}
