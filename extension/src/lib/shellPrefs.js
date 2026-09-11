// Appearance (light / dark / follow the OS) and the interface-motion mode: the
// window.StencilTheme / window.StencilMotion facades, plus the cross-page storage sync
// that re-applies all three when another extension page changes one. LAST of the
// pre-paint classic scripts — see accent.js.
(function () {
  var K = window.StencilKit;
  var EDGE_POINTS = K.EDGE_POINTS, KEY = K.KEY, MKEY = K.MKEY, MOTION_DEFAULT = K.MOTION_DEFAULT, MOTION_LABELS = K.MOTION_LABELS;
  var MOTION_MODES = K.MOTION_MODES, apply = K.apply, applyMotion = K.applyMotion, edgeJitter = K.edgeJitter, edgePolygon = K.edgePolygon;
  var grainShape = K.grainShape, isMotion = K.isMotion, mirror = K.mirror, motionReduced = K.motionReduced, paletteCss = K.paletteCss;
  var particleStyle = K.particleStyle, read = K.read, readMotion = K.readMotion, readPref = K.readPref, shapePolygon = K.shapePolygon;
  var stopOfTint = K.stopOfTint, styleFrame = K.styleFrame, swap = K.swap, tintOf = K.tintOf, writePref = K.writePref;
  // ── Appearance (light / dark / follow the OS) ───────────────────────────────
  // Stores the CHOSEN mode but stamps the RESOLVED one on <html data-theme="…">, so
  // lib/theme.css needs a single dark palette. Mirrors browser/js/prePaintTheme.js.
  var TKEY = 'stencil_theme';
  var MODES = ['system', 'light', 'dark'];
  var isMode = function (m) { return MODES.indexOf(m) >= 0; };
  var darkQuery = function () {
    try { return window.matchMedia('(prefers-color-scheme: dark)'); } catch (e) { return null; }
  };
  var readTheme = function () { return readPref(TKEY, isMode, 'system'); };
  var resolveTheme = function (mode) {
    if (mode === 'light' || mode === 'dark') return mode;
    var q = darkQuery();
    return q && q.matches ? 'dark' : 'light';
  };
  var applyTheme = function (mode) {
    document.documentElement.setAttribute('data-theme', resolveTheme(mode));
    mirror({ stencil_theme: mode });
  };
  applyTheme(readTheme());
  // 'system' has to keep tracking the OS: the attribute, not the media query, now
  // drives the palette.
  if (darkQuery()) {
    darkQuery().addEventListener('change', function () {
      if (readTheme() === 'system') swap(function () { applyTheme('system'); }, 'theme-toggle');
    });
  }
  window.StencilTheme = {
    modes: MODES,
    storageKey: TKEY,
    get: readTheme,
    resolved: function () { return resolveTheme(readTheme()); },
    // `from`: the control that was pressed (element or id) — see StencilAccent.set.
    set: function (mode, from) {
      var next = isMode(mode) ? mode : 'system';
      // Only the PALETTE is worth animating. Moving between two modes that resolve to the
      // same one — System while the OS is dark, then Dark — repaints nothing, so playing
      // the wipe for it is an animation over an unchanged screen. Store the mode, stamp it,
      // and stay still.
      var repaints = resolveTheme(next) !== resolveTheme(readTheme());
      writePref(TKEY, next);
      if (repaints) swap(function () { applyTheme(next); }, from || 'theme-toggle');
      else applyTheme(next);
      return next;
    },
    // Fires when ANOTHER surface changes the mode (see the storage listener below).
    onChange: function (fn) {
      try {
        window.addEventListener('storage', function (e) {
          if (e.key === TKEY || e.key === null) fn(readTheme());
        });
      } catch (e) { /* no window — not a page context */ }
    },
  };

  applyMotion(readMotion());
  window.StencilMotion = {
    modes: MOTION_MODES,
    labels: MOTION_LABELS,
    storageKey: MKEY,
    get: readMotion,
    // Store, restamp <html data-motion> for the CSS half, and answer the mode kept. No
    // wipe: a motion change repaints nothing.
    set: function (mode) {
      var next = isMotion(mode) ? mode : MOTION_DEFAULT;
      writePref(MKEY, next);
      applyMotion(next);
      return next;
    },
    // The gates lib/motion.js asks (browser motionPrefs.js motionReduced / dustEnabled /
    // particleStyle): nothing may move; particles may fly; and which style they wear.
    reduced: motionReduced,
    particles: function () { return particleStyle() !== null; },
    style: particleStyle,
    // The classic-script twin of lib/dustCloud.js styleFrame, published so the tests
    // can pin the two frame for frame.
    styleFrame: function (style, p, away, w, len, tMs) { return styleFrame(style, p, away, w, len, tMs, {}); },
    edgeJitter: function (style, k, points) { return edgeJitter(style, k, points || EDGE_POINTS); },
    edgePolygon: function (x, y, w, h, grow, style) { return edgePolygon(x, y, w, h, grow, style); },
    grainShape: grainShape,
    shapePolygon: function (shape, x, y, r, a) { return shapePolygon(shape, x, y, r, a, []); },
    tintOf: tintOf,
    stopOfTint: stopOfTint,
    paletteCss: paletteCss,
    // Fires when ANOTHER surface changes the mode (see the storage listener below).
    onChange: function (fn) {
      try {
        window.addEventListener('storage', function (e) {
          if (e.key === MKEY || e.key === null) fn(readMotion());
        });
      } catch (e) { /* no window — not a page context */ }
    },
  };

  // Live cross-page sync: localStorage is shared across all same-origin extension pages,
  // and the `storage` event fires in every OTHER document when one writes KEY — so an
  // accent change in the options page reaches an already-open side panel / DevTools panel
  // without a reload. (No echo: the event never fires in the document that wrote.)
  try {
    window.addEventListener('storage', function (e) {
      // Animated here too: this page is repainting for a change made elsewhere, and a
      // silent palette jump reads as a glitch. No pointer here, so it blooms from the centre.
      if (e.key === KEY || e.key === null) swap(function () { apply(read()); }, 'theme-toggle');   // key===null on localStorage.clear()
      if (e.key === TKEY || e.key === null) swap(function () { applyTheme(readTheme()); }, 'theme-toggle');
      if (e.key === MKEY || e.key === null) applyMotion(readMotion());   // the CSS half; motion.js reads live
    });
  } catch (e) { /* no window — not a page context */ }
})();
