import { icon } from '../ui/icons.js';
import { ACCENT_STORAGE_KEY, DEFAULT_ACCENT, isAccent, applyAccentFavicon, applyFaviconHex, normalizeHex } from './accents.js';

// ── AccentController: UI theme + accent preset/custom colour ────────
// Extracted from drawingApp.js. Holds no state — theme/accent live on the document element
// (data-theme / data-accent / inline --accent), so the app keeps the theme/accent/customAccent
// getters that read them, and this controller does the writes. Back-references the app for the
// theme-icon lookup and the cross-tab accent broadcast (app.tabs). Wired from the constructor's
// tabs.onAccent callback and #wireTheme (both call updateThemeIcon / applyAccent).
export class AccentController {
  constructor(app) {
    this.app = app;
  }

  // Set (and persist as the manual override) the UI theme; refreshes the toggle icon.
  setTheme(theme) {
    const next = String(theme).toLowerCase() === 'dark' ? 'dark' : 'light';
    document.documentElement.setAttribute('data-theme', next);
    localStorage.setItem('drawingApp_theme', next);
    this.updateThemeIcon();
  }

  updateThemeIcon() {
    const btn = document.getElementById('theme-toggle');
    if (btn) btn.innerHTML = this.app.theme === 'dark' ? icon('sun') : icon('moon');
  }

  // Set (and persist) the accent preset; --accent-2 and the glows derive from it.
  // Broadcasts to peer tabs so they repaint live (see applyAccent for the local apply).
  setAccent(key) {
    const next = this.applyAccent(key);
    this.app.tabs.broadcastAccent(next);
  }

  // Paint + persist the accent in THIS tab. Returns the resolved key. Used by
  // setAccent (local change) and the cross-tab listener (remote change, no re-broadcast).
  applyAccent(key) {
    const next = isAccent(key) ? key : DEFAULT_ACCENT;
    document.documentElement.style.removeProperty('--accent'); // drop any custom (temp) override
    document.documentElement.setAttribute('data-accent', next);
    try { localStorage.setItem(ACCENT_STORAGE_KEY, next); } catch { /* storage blocked — accent still applies this session, just won't persist */ }
    applyAccentFavicon(next);
    return next;
  }

  // Apply an arbitrary hex colour as the accent for this page only: NO persistence and NO
  // cross-tab broadcast (unlike setAccent), so it stays local and vanishes on reload. The
  // inline --accent overrides the data-accent preset rule; everything else derives from it.
  // Returns the normalized '#rrggbb', or null when `hex` isn't a valid colour.
  setCustomAccent(hex) {
    const norm = normalizeHex(hex);
    if (!norm) return null;
    document.documentElement.style.setProperty('--accent', norm);
    applyFaviconHex(norm);
    return norm;
  }
}
