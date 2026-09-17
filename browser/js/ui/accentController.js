import { icon } from './icons.js';
import { themeSwap, originOf, originOfId } from './motion.js';
import { ACCENT_STORAGE_KEY, DEFAULT_ACCENT, isAccent, applyAccentFavicon, applyFaviconHex, normalizeHex, accentHex, needsDarkGlyph } from '../core/accents.js';
import { publish, EVENTS } from '../eventBus/appBus.js';

// Three appearance modes, like the desktop and the extension; 'system' is the default.
// Same key as the pre-paint script (js/prePaintTheme.js), which resolves 'system' first.
export const THEME_STORAGE_KEY = 'drawingApp_theme';
export const THEME_MODES = Object.freeze(['system', 'light', 'dark']);

// 'system' is answered by the OS at call time, never stored resolved.
export const resolveThemeMode = (mode, prefersDark = typeof matchMedia === 'function'
  && matchMedia('(prefers-color-scheme: dark)').matches) =>
  (mode === 'dark' || mode === 'light') ? mode : (prefersDark ? 'dark' : 'light');

// Wipe origin: whichever of these toolbar icons is on screen; null ⇒ viewport centre.
const themeOrigin = () => originOfId('theme-toggle') || originOfId('visuals-btn') || originOfId('settings-btn');
const accentOrigin = () => originOfId('visuals-btn') || originOfId('theme-toggle') || originOfId('settings-btn');

// Theme/accent live on the document element (data-theme / data-accent / --accent);
// this controller does the writes, the app keeps the getters.
export class AccentController {
  #preview = null;
  constructor(app) {
    this.app = app;
  }

  setTheme(theme, originEl = null) {
    this.setThemeMode(String(theme).toLowerCase() === 'dark' ? 'dark' : 'light', originEl);
  }

// Same tri-state as the desktop (io/fileStore.hpp themeMode) and the extension
// (lib/shellTheme.js THEME_MODES). `originEl` is the control the wipe floods out of.
  setThemeMode(mode, originEl = null) {
    const next = THEME_MODES.includes(mode) ? mode : 'system';
// Only a palette change animates: two modes resolving to the same palette repaint nothing.
    const painted = document.documentElement.getAttribute('data-theme');
    if (resolveThemeMode(next) === painted) {
      localStorage.setItem(THEME_STORAGE_KEY, next);
      this.updateThemeIcon();
      publish(EVENTS.themeChanged, next);
      return;
    }
    themeSwap(() => {
// 'system' is stored as itself and resolved for painting (controlsBinder follows the OS).
      document.documentElement.setAttribute('data-theme', resolveThemeMode(next));
      localStorage.setItem(THEME_STORAGE_KEY, next);
      this.updateThemeIcon();
// The Appearance row in Default Visuals mirrors the toolbar's toggle.
      publish(EVENTS.themeChanged, next);
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

// Every copy of the button: the fullscreen layer clones the toolbar with its ids.
  updateThemeIcon() {
    const glyph = this.app.theme === 'dark' ? icon('sun') : icon('moon');
    for (const btn of document.querySelectorAll('[id="theme-toggle"]')) btn.innerHTML = glyph;
  }

// A light accent flags <html> so accent-backed controls take the dark ink (theme.css
// --on-accent); mirrors prePaintTheme.js.
  applyGlyphContrast(hex) {
    document.documentElement.toggleAttribute('data-accent-light', needsDarkGlyph(hex));
  }

  setAccent(key, originEl = null) {
    const next = this.applyAccent(key, originEl);
    this.app.tabs.broadcastAccent(next);
    this.announce(next);
  }

// The new value rides in `detail`: themeSwap writes the attribute a beat later.
  announce(value) {
    publish(EVENTS.accentChanged, value);
  }

// Paint + persist the accent in this tab; returns the resolved key.
  applyAccent(key, originEl = null) {
    this.#preview = null;
    const next = isAccent(key) ? key : DEFAULT_ACCENT;
    themeSwap(() => {
      document.documentElement.style.removeProperty('--accent'); // drop any custom (temp) override
      document.documentElement.setAttribute('data-accent', next);
      try { localStorage.setItem(ACCENT_STORAGE_KEY, next); } catch { /* storage blocked — accent still applies this session, just won't persist */ }
      applyAccentFavicon(next);
      this.applyGlyphContrast(accentHex(next));
    }, () => originOf(originEl) || accentOrigin());
    return next;
  }

// Hover preview: the same flood, no persist/favicon/broadcast. The first call snapshots
// the committed accent; endAccentPreview() floods back to it.
  previewAccent(key, originEl = null) {
    if (!isAccent(key)) return;
    const el = document.documentElement;
    if (!this.#preview) this.#preview = { data: el.getAttribute('data-accent'),
                                          inline: el.style.getPropertyValue('--accent') };
    themeSwap(() => {
      el.style.removeProperty('--accent');
      el.setAttribute('data-accent', key);
      this.applyGlyphContrast(accentHex(key));
    }, () => originOf(originEl) || accentOrigin());
  }

  endAccentPreview(originEl = null) {
    if (!this.#preview) return;
    const el = document.documentElement;
    const { data, inline } = this.#preview;
    this.#preview = null;
    themeSwap(() => {
      if (inline) el.style.setProperty('--accent', inline); else el.style.removeProperty('--accent');
      if (data) el.setAttribute('data-accent', data); else el.removeAttribute('data-accent');
      this.applyGlyphContrast(inline || accentHex(data || DEFAULT_ACCENT));
    }, () => originOf(originEl) || accentOrigin());
  }

// Page-only: no persistence, no broadcast. Returns '#rrggbb' or null when invalid.
  setCustomAccent(hex, originEl = null) {
    const norm = normalizeHex(hex);
    if (!norm) return null;
    this.#preview = null;
    themeSwap(() => {
      document.documentElement.style.setProperty('--accent', norm);
      applyFaviconHex(norm);
      this.applyGlyphContrast(norm);
    }, () => originOf(originEl) || accentOrigin());
    this.announce(norm);
    return norm;
  }
}
