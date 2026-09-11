// Accent (brand-colour) presets, the interface-motion mode, and the flash-free reads
// they share. FIRST of the pre-paint classic scripts (see accent.js for the set and why
// they are classic, not modules): everything after it reads what this file publishes on
// window.StencilKit. Choice lives in localStorage (synchronous + shared across
// same-origin pages, unlike async chrome.storage.sync, which would flash). ACCENTS is a
// checked-in copy of the canonical browser/js/config/accents.json (a classic script
// can't import JSON) — pinned by tests/dataParity.test.js.
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
    { key: 'grey',    label: 'Gray',        hex: '#64748b' },
  ];
  var KEY = 'stencil_accent';
  var DEFAULT = 'violet';
  var has = function (k) {
    return ACCENTS.some(function (a) { return a.key === k; });
  };
  // Both prefs below are a validated localStorage read/write (private mode can throw).
  var readPref = function (key, valid, fallback) {
    try {
      var v = localStorage.getItem(key);
      return valid(v) ? v : fallback;
    } catch (e) {
      return fallback;
    }
  };
  var writePref = function (key, v) {
    try { localStorage.setItem(key, v); } catch (e) { /* private mode */ }
  };
  // Mirror into chrome.storage.local for contexts that can't read this page's
  // localStorage (the service worker, the page-API bridge).
  var mirror = function (obj) {
    try {
      if (typeof chrome !== 'undefined' && chrome.storage && chrome.storage.local)
        chrome.storage.local.set(obj);
    } catch (e) { /* no chrome.storage on this page */ }
  };
  var read = function () { return readPref(KEY, has, DEFAULT); };

  // ── Interface motion (browser parity: js/ui/motionPrefs.js) ────────────────
  // 'particles' (dust — the default) | 'water' | 'fire' | 'slide' (each surface keeps its
  // own CSS entrance) | 'none'. Stamped on <html data-motion> before first paint for
  // lib/animations.css; the OS's prefers-reduced-motion still wins. Same localStorage
  // recipe as the accent, so it reaches every open extension page at once.
  var MKEY = 'stencil_motion';
  var MOTION_DEFAULT = 'particles';
  var MOTION_MODES = ['particles', 'water', 'fire', 'slide', 'none'];
  // The browser's MOTION_MODE_LABELS, in its order — what the options page offers.
  var MOTION_LABELS = [
    ['particles', 'Dust'],
    ['water', 'Water'],
    ['fire', 'Fire'],
    ['slide', 'Sliding'],
    ['none', 'None'],
  ];
  var PARTICLE_STYLE = { particles: 'dust', water: 'water', fire: 'fire' };
  var isMotion = function (m) { return MOTION_MODES.indexOf(m) >= 0; };
  var readMotion = function () { return readPref(MKEY, isMotion, MOTION_DEFAULT); };
  var osReduced = function () {
    try { return window.matchMedia('(prefers-reduced-motion: reduce)').matches; } catch (e) { return false; }
  };
  var motionReduced = function () { return readMotion() === 'none' || osReduced(); };
  // The style the particles wear — 'dust' | 'water' | 'fire' — or null when none fly.
  var particleStyle = function () {
    var s = PARTICLE_STYLE[readMotion()];
    return s && !osReduced() ? s : null;
  };
  var applyMotion = function (mode) {
    document.documentElement.setAttribute('data-motion', isMotion(mode) ? mode : MOTION_DEFAULT);
  };
  var hexOf = function (k) {
    for (var i = 0; i < ACCENTS.length; i++) if (ACCENTS[i].key === k) return ACCENTS[i].hex;
    return ACCENTS[0].hex;
  };
  // Accent-backed controls paint their label and line-art ON the accent, so the accent
  // picks the ink: whichever of white / near-black contrasts more (<html
  // data-accent-light> → lib/theme.css --on-accent). Browser twin: accents.js.
  var ON_ACCENT_LIGHT = '#ffffff';
  var ON_ACCENT_DARK = '#1a1a1a';
  var srgbToLinear = function (c) {
    return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
  };
  var relativeLuminance = function (hex) {
    var h = String(hex || '').replace(/^#/, '');
    if (!/^[0-9a-fA-F]{6}$/.test(h)) return null;
    var r = srgbToLinear(parseInt(h.slice(0, 2), 16) / 255);
    var g = srgbToLinear(parseInt(h.slice(2, 4), 16) / 255);
    var b = srgbToLinear(parseInt(h.slice(4, 6), 16) / 255);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
  };
  var needsDarkGlyph = function (hex) {
    var l = relativeLuminance(hex);
    return l != null && (l + 0.05) / 0.05 > 1.05 / (l + 0.05);
  };
  var onAccentInk = function (hex) {
    return needsDarkGlyph(hex) ? ON_ACCENT_DARK : ON_ACCENT_LIGHT;
  };

  // Tab favicon as inline SVG with the panel outline painted in `hex` (rest is fixed
  // brand art) — so an extension page opened as a tab (options) shows the Stencil mark
  // tinted to the accent. Mirrors the inline header logo and browser accents.js faviconSvg.
  var faviconSvg = function (hex) {
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">' +
      '<rect x="2" y="2" width="60" height="60" rx="13" fill="#2b2f3a"/>' +
      '<rect x="2.75" y="2.75" width="58.5" height="58.5" rx="12.25" fill="none" stroke="' + hex + '" stroke-width="1.5"/>' +
      '<rect x="12" y="12" width="40" height="40" rx="4" fill="#3a3f4b"/>' +
      '<polyline points="44,20 32,16 20,24 32,32 44,40 32,48 20,44" fill="none" stroke="#FFFF00" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>' +
      '<g fill="#FFFF00" stroke="#000000" stroke-width="1.25">' +
      '<circle cx="44" cy="20" r="2.6"/><circle cx="32" cy="16" r="2.6"/><circle cx="20" cy="24" r="2.6"/><circle cx="32" cy="32" r="2.6"/><circle cx="44" cy="40" r="2.6"/><circle cx="32" cy="48" r="2.6"/><circle cx="20" cy="44" r="2.6"/>' +
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

  // The shared page scope the six scripts after this one read (see accent.js).
  window.StencilKit = {
    ACCENTS: ACCENTS, DEFAULT: DEFAULT, KEY: KEY, MKEY: MKEY,
    MOTION_DEFAULT: MOTION_DEFAULT, MOTION_LABELS: MOTION_LABELS, MOTION_MODES: MOTION_MODES, applyFavicon: applyFavicon,
    applyMotion: applyMotion, faviconSvg: faviconSvg, has: has, hexOf: hexOf,
    isMotion: isMotion, mirror: mirror, motionReduced: motionReduced, needsDarkGlyph: needsDarkGlyph,
    onAccentInk: onAccentInk, particleStyle: particleStyle, read: read, readMotion: readMotion,
    readPref: readPref, writePref: writePref,
  };
})();
