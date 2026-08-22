// Apply saved theme + accent to <html> BEFORE first paint to avoid a wrong-colour
// flash. Loaded as a CLASSIC <script> in index.html's <head> (modules defer → flash),
// which also prevents importing js/core/accents.js — so the storage keys are inlined
// here, kept in sync with ACCENT_STORAGE_KEY / 'drawingApp_theme' there. localStorage
// reads are guarded: private/disabled storage throws, so we fall back to the system
// colour scheme and the :root default accent (violet).
(() => {
  const root = document.documentElement;

  let savedTheme = null;
  try {
    savedTheme = localStorage.getItem('drawingApp_theme');
  } catch {
    /* storage blocked (private mode) — fall back to the system colour scheme below */
  }
  const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
  // 'system' is a stored value like any other (accentController THEME_MODES) and resolves
  // exactly as an unset one does — the OS decides, here and on every later change.
  const explicit = savedTheme === 'dark' || savedTheme === 'light';
  root.setAttribute('data-theme', explicit ? savedTheme : (prefersDark ? 'dark' : 'light'));

  // An unknown/missing accent leaves the :root default (violet); see css/theme.css.
  // The light presets are inlined for the same reason the storage keys are — this
  // runs as a classic script and can't import accents.js. Kept in sync with its
  // derived LIGHT_ACCENT_KEYS (the accent tests assert the two lists match).
  const LIGHT_ACCENT_KEYS = ['yellow', 'sky'];
  try {
    const accent = localStorage.getItem('drawingApp_accent');
    if (accent) {
      root.setAttribute('data-accent', accent);
      // White glyphs on a light accent need their dark shadow from the first paint.
      if (LIGHT_ACCENT_KEYS.includes(accent)) root.setAttribute('data-accent-light', '');
    }
  } catch {
    /* storage blocked — keep the default accent */
  }
})();
