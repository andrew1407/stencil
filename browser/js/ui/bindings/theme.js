import { applyAccentFavicon } from '../../core/accents.js';
export function wireTheme(app) {
  app.accents.updateThemeIcon();
  // Tint the tab favicon + status bar to the saved accent on load.
  applyAccentFavicon(app.accent);
  document.getElementById('theme-toggle').addEventListener('click', (e) => {
    // Hand the wipe the button that was actually pressed — the fullscreen layer clones
    // this toolbar with duplicate ids, so looking it up by id can find a hidden copy.
    app.setTheme(app.theme === 'dark' ? 'light' : 'dark', e.currentTarget);
  });
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', e => {
    if (app.accents.themeMode !== 'system') return;
    document.documentElement.setAttribute('data-theme', e.matches ? 'dark' : 'light');
    app.accents.updateThemeIcon();
  });
}
