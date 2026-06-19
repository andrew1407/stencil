// Apply the saved theme + accent to <html> BEFORE first paint, so there's no flash
// of the wrong colours. Loaded as a CLASSIC <script> in index.html's <head> (not a
// module — modules defer, which would flash). It can't import js/core/accents.js for
// the same reason, so the storage keys are inlined here and kept in sync with
// ACCENT_STORAGE_KEY / 'drawingApp_theme' there. localStorage reads are guarded:
// private mode (or disabled storage) makes them throw, in which case we fall back to
// the system colour scheme and the :root default accent (violet).
(() => {
  const root = document.documentElement;

  let savedTheme = null;
  try {
    savedTheme = localStorage.getItem('drawingApp_theme');
  } catch {
    /* storage blocked (private mode) — fall back to the system colour scheme below */
  }
  const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
  root.setAttribute('data-theme', savedTheme || (prefersDark ? 'dark' : 'light'));

  // An unknown/missing accent leaves the :root default (violet); see css/theme.css.
  try {
    const accent = localStorage.getItem('drawingApp_accent');
    if (accent) root.setAttribute('data-accent', accent);
  } catch {
    /* storage blocked — keep the default accent */
  }
})();
