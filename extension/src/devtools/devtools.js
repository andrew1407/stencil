// Add a "Stencil" tab to DevTools (alongside Elements, Console, …). The panel page
// reuses the popup controller, but bound to the inspected tab instead of the active
// one. Icon/page paths are relative to the extension root, not this file.
chrome.devtools.panels.create(
  'Stencil',
  'icons/icon-32.png',
  'src/devtools/panel.html'
);
