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
    // page can answer 'system' differently. lib/prefs/shellTheme.js reads it back.
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

  // An earlier build parked the user's own mode under a skin's: the mode goes back.
  const HKEY = 'stencil_motion_held', parked = readPref(HKEY, isMotion, '');
  if (parked) { writePref(MKEY, parked); writePref(HKEY, ''); }
  applyMotion(readMotion());
  window.StencilMotion = {
    modes: MOTION_MODES,
    labels: MOTION_LABELS,
    storageKey: MKEY,
    get: readMotion,
    // No wipe: a motion change repaints nothing. The user's pick ends a skin's session mode.
    set: function (mode) {
      const next = isMotion(mode) ? mode : MOTION_DEFAULT;
      writePref(MKEY, next);
      applyMotion(next);
      try { window.dispatchEvent(new CustomEvent('stencil:motion-changed', { detail: next })); } catch (e) { /* no DOM */ }
      return next;
    },
    stored: readMotion,
    // The gates lib/motion.js asks (browser prefs.js motionReduced / dustEnabled / particleStyle).
    reduced: motionReduced,
    particles: function () { return particleStyle() !== null; },
    style: particleStyle,
    // Classic-script twin of lib/dust/cloud.js styleFrame; the tests pin the two frame for frame.
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
        window.addEventListener('stencil:motion-changed', function () { fn(readMotion()); });
      } catch (e) { /* no window — not a page context */ }
    },
  };

  // The `storage` event fires in every OTHER same-origin extension document, never in the
  // writer — so a change in the options page reaches an open side panel without an echo.
  try {
    window.addEventListener('storage', function (e) {
      // Animated here too: a silent palette jump reads as a glitch.
      if (e.key === KEY || e.key === null) swap(function () { apply(read()); }, 'theme-toggle', true);   // an accent: from the top   // key===null on localStorage.clear()
      if (e.key === TKEY || e.key === null) swap(function () { applyTheme(readTheme()); }, 'theme-toggle');
      if (e.key === MKEY || e.key === null) applyMotion(readMotion());   // the CSS half; motion.js reads live
    });
  } catch (e) { /* no window — not a page context */ }
})();

// The skins (browser css/webcore/, css/aqua/): the one on is stored and stamped on every page. Its word
// stores its look as the user's own (webcore: still and Gray, aqua: Sliding and Blue); an off on the same
// page gives back what the look replaced, unless the user picked their own. Browser twin: ui/skin/adopt.js.
(function () {
  const readPref = window.StencilKit.readPref, writePref = window.StencilKit.writePref;
  const readMotion = function () { return window.StencilMotion.get(); };
  const SKEY = 'stencil_skin', SKIN = 'webcore', AQUA = 'aqua';
  const LOOK = Object.freeze({ webcore: { motion: 'none', accent: 'grey' }, aqua: { motion: 'slide', accent: 'blue' } });
  const readName = function () { return readPref(SKEY, function (v) { return v === SKIN || v === AQUA; }, ''); };
  const applySkin = function (name) {
    if (name) document.documentElement.setAttribute('data-skin', name);
    else document.documentElement.removeAttribute('data-skin');
  };
  const accentNow = function () { return window.StencilAccent ? window.StencilAccent.get() : ''; };
  let replaced = null;   // { name, motion, accent } the word's look replaced on this page
  const wearLook = function (motion, accent) {
    if (readMotion() !== motion) window.StencilMotion.set(motion);
    if (window.StencilAccent && accentNow() !== accent) window.StencilAccent.set(accent);
  };
  // Each half of the look the user left alone goes back to what it replaced.
  const giveBack = function () {
    const was = replaced, look = was && LOOK[was.name];
    replaced = null;
    if (!look) return;
    wearLook(readMotion() === look.motion ? was.motion : readMotion(), accentNow() === look.accent ? was.accent : accentNow());
  };
  const setName = function (name) {
    const was = readName();
    if (was && was !== name) giveBack();
    if (name && name !== was) {
      replaced = { name: name, motion: readMotion(), accent: accentNow() };
      wearLook(LOOK[name].motion, LOOK[name].accent);
    }
    writePref(SKEY, name); applySkin(name); return name;
  };
  // One skin's switch: on stamps it; off clears it only while it is the one stamped.
  const switchFor = function (skin) {
    return {
      get: function () { return readName() === skin; },
      set: function (on) { return setName(on ? skin : (readName() === skin ? '' : readName())) === skin; },
    };
  };
  applySkin(readName());
  const webcore = switchFor(SKIN);
  window.StencilSkin = { storageKey: SKEY, get: webcore.get, set: webcore.set, name: readName, aqua: switchFor(AQUA) };
  try {
    window.addEventListener('storage', function (e) {   // another page turned a skin on or off
      if (e.key === SKEY || e.key === null) applySkin(readName());
    });
  } catch (e) { /* no window — not a page context */ }
})();
