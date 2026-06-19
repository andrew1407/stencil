// Accent (brand-colour) presets + flash-free apply for the extension's own pages
// (popup, options, sidepanel, crop, devtools panel). Loaded as a CLASSIC <script>
// (separate file — MV3 forbids inline page scripts) in each page's <head> BEFORE
// lib/theme.css, so the saved accent sits on <html data-accent="…"> before first paint.
// Choice lives in localStorage (synchronous + shared across same-origin pages, unlike
// async chrome.storage.sync which would flash). --accent-2 shade and glows derive from
// --accent via color-mix() in lib/theme.css. Mirrors browser/js/core/accents.js and
// desktop theme.cpp. window.StencilAccent lets the options page read/write it.
(function () {
  var ACCENTS = [
    { key: 'violet',  label: 'Violet',      hex: '#7c3aed' },
    { key: 'pink',    label: 'Pink',        hex: '#ec4899' },
    { key: 'yellow',  label: 'Yellow',      hex: '#eab308' },
    { key: 'orange',  label: 'Orange',      hex: '#ea580c' },
    { key: 'crimson', label: 'Crimson',     hex: '#be123c' },
    { key: 'aqua',    label: 'Aqua',        hex: '#0891b2' },
    { key: 'sky',     label: 'Sky blue',    hex: '#0ea5e9' },
    { key: 'blue',    label: 'Blue',        hex: '#2563eb' },
    { key: 'grass',   label: 'Grass green', hex: '#16a34a' },
    { key: 'green',   label: 'Green',       hex: '#047857' },
    { key: 'brown',   label: 'Brown',       hex: '#a87c50' },
    { key: 'grey',    label: 'Grey',        hex: '#64748b' },
  ];
  var KEY = 'stencil_accent';
  var DEFAULT = 'violet';
  var has = function (k) {
    return ACCENTS.some(function (a) { return a.key === k; });
  };
  var read = function () {
    try {
      var v = localStorage.getItem(KEY);
      return has(v) ? v : DEFAULT;
    } catch (e) {
      return DEFAULT;
    }
  };
  var hexOf = function (k) {
    for (var i = 0; i < ACCENTS.length; i++) if (ACCENTS[i].key === k) return ACCENTS[i].hex;
    return ACCENTS[0].hex;
  };
  // Tab favicon as inline SVG with the panel outline painted in `hex` (rest is fixed
  // brand art) — so an extension page opened as a tab (options) shows the Stencil mark
  // tinted to the accent. Mirrors the inline header logo and browser accents.js faviconSvg.
  var faviconSvg = function (hex) {
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">' +
      '<rect x="2" y="2" width="60" height="60" rx="13" fill="#2b2f3a"/>' +
      '<rect x="2.75" y="2.75" width="58.5" height="58.5" rx="12.25" fill="none" stroke="' + hex + '" stroke-width="1.5"/>' +
      '<rect x="12" y="12" width="40" height="40" rx="4" fill="#3a3f4b"/>' +
      '<polyline points="16,46 27,24 38,38 50,18" fill="none" stroke="#FFFF00" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>' +
      '<g fill="#FFFF00" stroke="#000000" stroke-width="1.25">' +
      '<circle cx="16" cy="46" r="3.4"/><circle cx="27" cy="24" r="3.4"/><circle cx="38" cy="38" r="3.4"/><circle cx="50" cy="18" r="3.4"/>' +
      '</g></svg>';
  };
  // Swap the <link rel="icon"> to a data-URL SVG carrying the accent (a static .svg
  // file the browser can't read our CSS var from). No-op until <head> exists.
  var applyFavicon = function (k) {
    if (typeof document === 'undefined' || !document.head) return;
    var link = document.querySelector('link[rel="icon"]');
    if (!link) { link = document.createElement('link'); link.rel = 'icon'; document.head.appendChild(link); }
    link.type = 'image/svg+xml';
    link.href = 'data:image/svg+xml,' + encodeURIComponent(faviconSvg(hexOf(has(k) ? k : DEFAULT)));
  };
  var apply = function (k) {
    document.documentElement.setAttribute('data-accent', has(k) ? k : DEFAULT);
    applyFavicon(k);
  };
  apply(read());
  window.StencilAccent = {
    list: ACCENTS,
    storageKey: KEY,
    get: read,
    set: function (k) {
      var next = has(k) ? k : DEFAULT;
      try { localStorage.setItem(KEY, next); } catch (e) { /* private mode */ }
      apply(next);
      return next;
    },
  };
})();
