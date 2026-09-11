// Apply saved theme + accent (and the motion mode) to <html> BEFORE first paint to
// avoid a wrong-colour flash — and, for `data-motion`, an entrance animation playing
// on a page whose owner turned animation off. Loaded as a CLASSIC <script> in index.html's <head> (modules defer → flash),
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
  const LIGHT_ACCENT_KEYS = ['pink', 'yellow', 'orange', 'aqua', 'sky', 'grass', 'brown'];
  try {
    const accent = localStorage.getItem('drawingApp_accent');
    if (accent) {
      root.setAttribute('data-accent', accent);
      // A light accent wants the dark on-accent ink from the very first paint.
      if (LIGHT_ACCENT_KEYS.includes(accent)) root.setAttribute('data-accent-light', '');
    }
  } catch {
    /* storage blocked — keep the default accent */
  }

  // Interface motion mode: 'particles' (default) | 'water' | 'fire' | 'slide' | 'none'. The CSS half of
  // js/ui/motionPrefs.js — animations/motionModes.css keys the no-motion rules off it,
  // and it must be on <html> before the app's own entrance plays. Same inlining rule as
  // above (classic script, no imports): keep the key and the values in step with
  // MOTION_STORAGE_KEY / MOTION_MODES there.
  try {
    const saved = JSON.parse(localStorage.getItem('drawingApp_motion') || 'null');
    const mode = saved && typeof saved === 'object' ? String(saved.mode) : '';
    root.setAttribute('data-motion',
      ['particles', 'water', 'fire', 'slide', 'none'].includes(mode) ? mode : 'particles');
  } catch {
    root.setAttribute('data-motion', 'particles');   /* storage blocked or junk — the default */
  }
})();
