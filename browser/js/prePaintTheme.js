// Applies the saved theme, accent and motion mode to <html> BEFORE first paint. A CLASSIC
// <script> in index.html's <head> (modules defer → flash), so it cannot import
// core/accents.js: the storage keys and light presets are inlined and kept in sync there.
(() => {
  const root = document.documentElement;

  let savedTheme = null;
  try {
    savedTheme = localStorage.getItem('drawingApp_theme');
  } catch {
    /* storage blocked (private mode) — fall back to the system colour scheme below */
  }
  const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
// 'system' resolves exactly as an unset value does (accentController THEME_MODES).
  const explicit = savedTheme === 'dark' || savedTheme === 'light';
  root.setAttribute('data-theme', explicit ? savedTheme : (prefersDark ? 'dark' : 'light'));

// An unknown accent leaves the :root default (css/theme.css). LIGHT_ACCENT_KEYS mirrors
// accents.js's derived list (the accent tests assert the two match).
  const LIGHT_ACCENT_KEYS = ['pink', 'orange', 'brown', 'yellow', 'grass', 'turquoise', 'aqua', 'sky', 'bluegray'];
  try {
    const accent = localStorage.getItem('drawingApp_accent');
    if (accent) {
      root.setAttribute('data-accent', accent);
// A light accent wants the dark on-accent ink from the first paint.
      if (LIGHT_ACCENT_KEYS.includes(accent)) root.setAttribute('data-accent-light', '');
    }
  } catch {
    /* storage blocked — keep the default accent */
  }

// The CSS half of ui/motionPrefs.js: animations/motionModes.css keys off data-motion, which
// must be on <html> before the entrance plays. Keep in step with MOTION_STORAGE_KEY/MOTION_MODES.
  try {
    const saved = JSON.parse(localStorage.getItem('drawingApp_motion') || 'null');
    const mode = saved && typeof saved === 'object' ? String(saved.mode) : '';
    root.setAttribute('data-motion',
      ['particles', 'water', 'fire', 'slide', 'none'].includes(mode) ? mode : 'particles');
  } catch {
    root.setAttribute('data-motion', 'particles');   /* storage blocked or junk — the default */
  }
})();
