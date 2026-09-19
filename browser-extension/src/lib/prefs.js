// First of the pre-paint classic scripts (see accent.js): publishes window.StencilKit.
// Choices live in localStorage — synchronous, unlike chrome.storage.sync, which would flash.
// ACCENTS is a checked-in copy of browser/js/config/accents.json, pinned by tests/dataParity.test.js.
(function () {
  const ACCENTS = [
    { key: 'violet',    label: 'Violet',      hex: '#7c3aed' },
    { key: 'burgundy',  label: 'Burgundy',    hex: '#660033' },
    { key: 'pink',      label: 'Pink',        hex: '#ec4899' },
    { key: 'crimson',   label: 'Crimson',     hex: '#be123c' },
    { key: 'maroon',    label: 'Maroon',      hex: '#550000' },
    { key: 'orange',    label: 'Orange',      hex: '#ea580c' },
    { key: 'brown',     label: 'Brown',       hex: '#a87c50' },
    { key: 'yellow',    label: 'Yellow',      hex: '#eab308' },
    { key: 'grass',     label: 'Grass green', hex: '#16a34a' },
    { key: 'green',     label: 'Green',       hex: '#047857' },
    { key: 'turquoise', label: 'Turquoise',   hex: '#40e0d0' },
    { key: 'aqua',      label: 'Aqua',        hex: '#0891b2' },
    { key: 'sky',       label: 'Sky blue',    hex: '#0ea5e9' },
    { key: 'bluegray',  label: 'Blue gray',   hex: '#7394b3' },
    { key: 'grey',      label: 'Gray',        hex: '#64748b' },
    { key: 'blue',      label: 'Blue',        hex: '#2563eb' },
  ];
  const KEY = 'stencil_accent';
  const DEFAULT = 'violet';
  const has = function (k) {
    return ACCENTS.some(function (a) { return a.key === k; });
  };
  // Private mode can throw.
  const readPref = function (key, valid, fallback) {
    try {
      const v = localStorage.getItem(key);
      return valid(v) ? v : fallback;
    } catch (e) {
      return fallback;
    }
  };
  const writePref = function (key, v) {
    try { localStorage.setItem(key, v); } catch (e) { /* private mode */ }
  };
  // For contexts that cannot read this page's localStorage (service worker, page-API bridge).
  const mirror = function (obj) {
    try {
      if (typeof chrome !== 'undefined' && chrome.storage && chrome.storage.local)
        chrome.storage.local.set(obj);
    } catch (e) { /* no chrome.storage on this page */ }
  };
  const read = function () { return readPref(KEY, has, DEFAULT); };

  // Interface motion (browser parity: js/ui/motionPrefs.js), stamped on <html data-motion>
  // before first paint for lib/animations/motionModes.css; prefers-reduced-motion still wins.
  const MKEY = 'stencil_motion';
  const MOTION_DEFAULT = 'particles';
  const MOTION_MODES = ['particles', 'water', 'fire', 'slide', 'none'];
  // The browser's MOTION_MODE_LABELS, in its order.
  const MOTION_LABELS = [
    ['particles', 'Dust'],
    ['water', 'Water'],
    ['fire', 'Fire'],
    ['slide', 'Sliding'],
    ['none', 'None'],
  ];
  const PARTICLE_STYLE = { particles: 'dust', water: 'water', fire: 'fire' };
  const isMotion = function (m) { return MOTION_MODES.indexOf(m) >= 0; };
  const readMotion = function () { return readPref(MKEY, isMotion, MOTION_DEFAULT); };
  const osReduced = function () {
    try { return window.matchMedia('(prefers-reduced-motion: reduce)').matches; } catch (e) { return false; }
  };
  const motionReduced = function () { return readMotion() === 'none' || osReduced(); };
  // null when no particles fly.
  const particleStyle = function () {
    const s = PARTICLE_STYLE[readMotion()];
    return s && !osReduced() ? s : null;
  };
  const applyMotion = function (mode) {
    document.documentElement.setAttribute('data-motion', isMotion(mode) ? mode : MOTION_DEFAULT);
  };
  const hexOf = function (k) {
    for (let i = 0; i < ACCENTS.length; i++) if (ACCENTS[i].key === k) return ACCENTS[i].hex;
    return ACCENTS[0].hex;
  };
  // The accent picks its ink: whichever of white / near-black contrasts more
  // (<html data-accent-light> → lib/theme/palette.css --on-accent). Browser twin: accents.js.
  const ON_ACCENT_LIGHT = '#ffffff';
  const ON_ACCENT_DARK = '#1a1a1a';
  const srgbToLinear = function (c) {
    return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
  };
  const relativeLuminance = function (hex) {
    const h = String(hex || '').replace(/^#/, '');
    if (!/^[0-9a-fA-F]{6}$/.test(h)) return null;
    const r = srgbToLinear(parseInt(h.slice(0, 2), 16) / 255);
    const g = srgbToLinear(parseInt(h.slice(2, 4), 16) / 255);
    const b = srgbToLinear(parseInt(h.slice(4, 6), 16) / 255);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
  };
  const needsDarkGlyph = function (hex) {
    const l = relativeLuminance(hex);
    return l != null && (l + 0.05) / 0.05 > 1.05 / (l + 0.05);
  };
  const onAccentInk = function (hex) {
    return needsDarkGlyph(hex) ? ON_ACCENT_DARK : ON_ACCENT_LIGHT;
  };

  // Mirrors browser accents.js faviconSvg (pinned by browser/tests/svgArt.test.js).
  const faviconSvg = function (hex) {
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">' +
      '<rect x="2" y="2" width="60" height="60" rx="13" fill="#2b2f3a"/>' +
      '<rect x="2.75" y="2.75" width="58.5" height="58.5" rx="12.25" fill="none" stroke="' + hex + '" stroke-width="1.5"/>' +
      '<rect x="12" y="12" width="40" height="40" rx="4" fill="#3a3f4b"/>' +
      '<polyline points="44,20 32,16 20,24 32,32 44,40 32,48 20,44" fill="none" stroke="#FFFF00" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>' +
      '<g fill="#FFFF00" stroke="#000000" stroke-width="1.25">' +
      '<circle cx="44" cy="20" r="2.6"/><circle cx="32" cy="16" r="2.6"/><circle cx="20" cy="24" r="2.6"/><circle cx="32" cy="32" r="2.6"/><circle cx="44" cy="40" r="2.6"/><circle cx="32" cy="48" r="2.6"/><circle cx="20" cy="44" r="2.6"/>' +
      '</g></svg>';
  };
  // A static .svg cannot read our CSS var, hence a data URL. No-op until <head> exists.
  const applyFavicon = function (k) {
    if (typeof document === 'undefined' || !document.head) return;
    let link = document.querySelector('link[rel="icon"]');
    if (!link) { link = document.createElement('link'); link.rel = 'icon'; document.head.appendChild(link); }
    link.type = 'image/svg+xml';
    link.href = 'data:image/svg+xml,' + encodeURIComponent(faviconSvg(hexOf(has(k) ? k : DEFAULT)));
  };

  window.StencilKit = {
    ACCENTS: ACCENTS, DEFAULT: DEFAULT, KEY: KEY, MKEY: MKEY,
    MOTION_DEFAULT: MOTION_DEFAULT, MOTION_LABELS: MOTION_LABELS, MOTION_MODES: MOTION_MODES, applyFavicon: applyFavicon,
    applyMotion: applyMotion, faviconSvg: faviconSvg, has: has, hexOf: hexOf,
    isMotion: isMotion, mirror: mirror, motionReduced: motionReduced, needsDarkGlyph: needsDarkGlyph,
    onAccentInk: onAccentInk, particleStyle: particleStyle, read: read, readMotion: readMotion,
    readPref: readPref, writePref: writePref,
  };
})();
