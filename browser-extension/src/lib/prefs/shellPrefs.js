// The window.StencilTheme / window.StencilMotion facades and the cross-page storage sync.
// Last of the pre-paint classic scripts — see accent.js.
(function () {
  const K = window.StencilKit;
  const EDGE_POINTS = K.EDGE_POINTS, KEY = K.KEY, MKEY = K.MKEY, MOTION_DEFAULT = K.MOTION_DEFAULT, MOTION_LABELS = K.MOTION_LABELS;
  const MOTION_MODES = K.MOTION_MODES, apply = K.apply, applyMotion = K.applyMotion, edgeJitter = K.edgeJitter, edgePolygon = K.edgePolygon;
  const grainShape = K.grainShape, isMotion = K.isMotion, mirror = K.mirror, motionReduced = K.motionReduced, paletteCss = K.paletteCss;
  const particleStyle = K.particleStyle, read = K.read, readMotion = K.readMotion, readPref = K.readPref, shapePolygon = K.shapePolygon;
  const stopOfTint = K.stopOfTint, styleFrame = K.styleFrame, swap = K.swap, tintOf = K.tintOf, writePref = K.writePref;
  // Stores the CHOSEN mode but stamps the RESOLVED one on <html data-theme>, so
  // lib/theme/ needs a single dark palette. Mirrors browser/js/prePaintTheme.js.
  const TKEY = 'stencil_theme';
  const MODES = ['system', 'light', 'dark'];
  const isMode = function (m) { return MODES.indexOf(m) >= 0; };
  const darkQuery = function () {
    try { return window.matchMedia('(prefers-color-scheme: dark)'); } catch (e) { return null; }
  };
  const readTheme = function () { return readPref(TKEY, isMode, 'system'); };
  const resolveTheme = function (mode) {
    if (mode === 'light' || mode === 'dark') return mode;
    const q = darkQuery();
    return q && q.matches ? 'dark' : 'light';
  };
  const applyTheme = function (mode) {
    const resolved = resolveTheme(mode);
    document.documentElement.setAttribute('data-theme', resolved);
    // The RESOLVED scheme travels too: the service worker has no matchMedia, and an injected
    // page can answer 'system' differently. lib/shellTheme.js reads it back.
    mirror({ stencil_theme: mode, stencil_theme_resolved: resolved });
  };
  applyTheme(readTheme());
  // The attribute, not the media query, drives the palette — so 'system' tracks the OS here.
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
      const next = isMode(mode) ? mode : 'system';
      // Two modes resolving to the same palette repaint nothing — no wipe over an unchanged screen.
      const repaints = resolveTheme(next) !== resolveTheme(readTheme());
      writePref(TKEY, next);
      if (repaints) swap(function () { applyTheme(next); }, from || 'theme-toggle');
      else applyTheme(next);
      return next;
    },
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
    // No wipe: a motion change repaints nothing.
    set: function (mode) {
      const next = isMotion(mode) ? mode : MOTION_DEFAULT;
      writePref(MKEY, next);
      applyMotion(next);
      return next;
    },
    // The gates lib/motion.js asks (browser prefs.js motionReduced / dustEnabled / particleStyle).
    reduced: motionReduced,
    particles: function () { return particleStyle() !== null; },
    style: particleStyle,
    // Classic-script twin of lib/cloud.js styleFrame; the tests pin the two frame for frame.
    styleFrame: function (style, p, away, w, len, tMs) { return styleFrame(style, p, away, w, len, tMs, {}); },
    edgeJitter: function (style, k, points) { return edgeJitter(style, k, points || EDGE_POINTS); },
    edgePolygon: function (x, y, w, h, grow, style) { return edgePolygon(x, y, w, h, grow, style); },
    grainShape: grainShape,
    shapePolygon: function (shape, x, y, r, a) { return shapePolygon(shape, x, y, r, a, []); },
    tintOf: tintOf,
    stopOfTint: stopOfTint,
    paletteCss: paletteCss,
    onChange: function (fn) {
      try {
        window.addEventListener('storage', function (e) {
          if (e.key === MKEY || e.key === null) fn(readMotion());
        });
      } catch (e) { /* no window — not a page context */ }
    },
  };

  // The `storage` event fires in every OTHER same-origin extension document, never in the
  // writer — so a change in the options page reaches an open side panel without an echo.
  try {
    window.addEventListener('storage', function (e) {
      // Animated here too: a silent palette jump reads as a glitch.
      if (e.key === KEY || e.key === null) swap(function () { apply(read()); }, 'theme-toggle');   // key===null on localStorage.clear()
      if (e.key === TKEY || e.key === null) swap(function () { applyTheme(readTheme()); }, 'theme-toggle');
      if (e.key === MKEY || e.key === null) applyMotion(readMotion());   // the CSS half; motion.js reads live
    });
  } catch (e) { /* no window — not a page context */ }
})();
