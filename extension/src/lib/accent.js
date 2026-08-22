// Accent (brand-colour) presets + flash-free apply for the extension's own pages
// (popup, options, sidepanel, crop, devtools panel). Loaded as a CLASSIC <script>
// (separate file — MV3 forbids inline page scripts) in each page's <head> BEFORE
// lib/theme.css, so the saved accent sits on <html data-accent="…"> before first paint.
// Choice lives in localStorage (synchronous + shared across same-origin pages, unlike
// async chrome.storage.sync which would flash). --accent-2 shade and glows derive from
// --accent via color-mix() in lib/theme.css. ACCENTS is a checked-in copy of the canonical
// browser/js/config/accents.json (a classic script can't import JSON) — pinned by
// tests/dataParity.test.js. window.StencilAccent lets the options page read/write it.
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
  var hexOf = function (k) {
    for (var i = 0; i < ACCENTS.length; i++) if (ACCENTS[i].key === k) return ACCENTS[i].hex;
    return ACCENTS[0].hex;
  };
  // Accent-backed controls paint white glyphs; a light accent washes them out, so
  // <html data-accent-light> switches on a dark shadow (lib/theme.css --glyph-shadow).
  // Same 3:1 threshold as browser accents.js and desktop theme.cpp.
  var GLYPH_SHADOW_MIN_CONTRAST = 3;
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
  var needsGlyphShadow = function (hex) {
    var l = relativeLuminance(hex);
    return l != null && 1.05 / (l + 0.05) < GLYPH_SHADOW_MIN_CONTRAST;
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
  // ── Palette swap animation (browser parity: browser/js/ui/motion.js themeSwap) ──
  // The new palette floods out of the CONTROL that changed it, as a growing circle, via
  // the native View Transitions API; without it every colour consumer gets one beat of
  // transition instead (lib/animations.css .theme-swapping). Hand-rolled rather than
  // imported: this file is a pre-paint classic script with no module graph.
  // ONLY user-initiated changes go through it — the boot-time apply must not animate.
  var SWAP_MS = 280;   // browser parity: motion.js THEME_SWAP_MS
  // The origin is the CONTROL that changed the palette — nothing else qualifies, and with
  // no control on screen the circle blooms from the viewport centre. It used to fall back
  // to the last pointerdown, which is what put the circle in a window corner: that press
  // is often unrelated (choosing "Dark" in the options page's native <select> fires no
  // pointerdown at all, so the previous click anywhere on the page won), and the palette
  // then floods out of the corner instead of the control. Same rule as
  // browser/js/ui/motion.js and desktop's applyTheme ("the cursor is NOT good enough").

  // Centre of an element that is actually ON SCREEN. A laid-out copy isn't necessarily a
  // visible one — `visibility: hidden` and `opacity: 0` both leave a good rect behind, and
  // an off-screen clone keeps its size — and blooming from one of those is how the wipe
  // ends up coming out of a corner.
  // Clipped out of sight by an ANCESTOR? checkVisibility below cannot see that: a control
  // inside a collapsed panel keeps a perfectly good rect and its own `visibility: visible`;
  // it is the ancestor's `overflow: hidden` that hides it. Port of browser motion.js
  // clippedAway — bloom from one of those and the wipe comes out of a corner.
  var clippedAway = function (el, r) {
    if (typeof getComputedStyle !== 'function') return false;
    for (var p = el.parentElement; p; p = p.parentElement) {
      var s = getComputedStyle(p);
      if (s.overflow === 'visible' && s.overflowX === 'visible' && s.overflowY === 'visible') continue;
      var b = p.getBoundingClientRect();
      if (r.right <= b.left || r.left >= b.right || r.bottom <= b.top || r.top >= b.bottom) return true;
    }
    return false;
  };

  var centreOf = function (el) {
    if (!el || !el.getBoundingClientRect) return null;
    var r = el.getBoundingClientRect();
    if (!r.width && !r.height) return null;   // hidden — not a place to start from
    if (el.parentElement && clippedAway(el, r)) return null;
    if (typeof el.checkVisibility === 'function' &&
        !el.checkVisibility({ visibilityProperty: true, opacityProperty: true, contentVisibilityAuto: true }))
      return null;
    var c = { x: r.left + r.width / 2, y: r.top + r.height / 2 };
    if (c.x < 0 || c.y < 0 || c.x > window.innerWidth || c.y > window.innerHeight) return null;
    return c;
  };

  // The control a swap belongs to, so the wipe comes out of the button even when there
  // was no click to read (a keyboard path, or a change pushed from another surface).
  // Takes an element, or an id — and for an id scans every element carrying it, not just
  // getElementById's first hit: a panel that clones its toolbar duplicates ids, and a
  // hidden copy winning the lookup is exactly how the wipe blooms from the wrong place.
  var originOf = function (ref) {
    if (!ref) return null;
    if (typeof ref !== 'string') return centreOf(ref);
    var all = document.querySelectorAll('[id="' + ref + '"]');
    for (var i = 0; i < all.length; i++) {
      var c = centreOf(all[i]);
      if (c) return c;
    }
    return null;
  };

  var swap = function (fn, originId) {
    var root = document.documentElement;
    var reduced = false;
    try { reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches; } catch (e) { /* assume motion is fine */ }
    if (reduced || typeof document.startViewTransition !== 'function') {
      if (!root || !root.classList) { fn(); return; }
      root.classList.add('theme-swapping');
      clearTimeout(root._themeSwapTimer);
      fn();
      root._themeSwapTimer = setTimeout(function () { root.classList.remove('theme-swapping'); }, SWAP_MS);
      return;
    }
    // Handed to the DECLARATIVE keyframes in lib/animations.css. Scripting the animation
    // from ready.then() instead races the transition's own teardown — it ends as soon as
    // its pseudo-elements have no animations, so the wipe stopped half way.
    // In PERCENTAGES of the viewport, never pixels: an engine that measures the pseudo-
    // element's box in device pixels paints a px origin at half its offset — the circle
    // blooming above and to the left of the button. Mirrors motion.js swapPercent.
    var write = function () {
      var w = window.innerWidth, h = window.innerHeight;
      var p = originOf(originId) || { x: w / 2, y: h / 2 };
      // The radius that reaches the furthest corner — the circle must cover the whole page.
      var r = Math.sqrt(Math.pow(Math.max(p.x, w - p.x), 2) + Math.pow(Math.max(p.y, h - p.y), 2));
      var pc = function (v) { return Math.round(v * 1000) / 1000; };
      var diag = Math.sqrt(w * w + h * h);
      root.style.setProperty('--swap-x', pc((100 * p.x) / w) + '%');
      root.style.setProperty('--swap-y', pc((100 * p.y) / h) + '%');
      root.style.setProperty('--swap-r', pc((100 * Math.SQRT2 * r) / diag) + '%');
      root.style.setProperty('--swap-ms', SWAP_MS + 'ms');
    };
    write();
    var t = document.startViewTransition(function () {
      // Transitions off while the snapshot is captured, or it records the OLD colours
      // mid-ease and the wipe reveals a half-changed page.
      root.classList.add('theme-instant');
      fn();
      // Re-asked after the write: the control can MOVE as the palette lands (a label that
      // changes width, a row that reflows), and the circle is painted against the new page.
      write();
    });
    var settle = function () { root.classList.remove('theme-instant'); };
    t.finished.then(settle, settle);
  };

  // The mirrored accent lets non-page contexts colour the on-page highlight to match
  // the theme (lib/highlightColor.js).
  var apply = function (k) {
    var next = has(k) ? k : DEFAULT;
    document.documentElement.setAttribute('data-accent', next);
    // White glyphs on a light accent get their dark shadow (lib/theme.css).
    if (needsGlyphShadow(hexOf(next))) document.documentElement.setAttribute('data-accent-light', '');
    else document.documentElement.removeAttribute('data-accent-light');
    applyFavicon(k);
    mirror({ stencil_accent: next });
  };
  apply(read());
  window.StencilAccent = {
    list: ACCENTS,
    storageKey: KEY,
    get: read,
    hexOf: hexOf,
    // `from` is the control that was pressed (element or id) — the options page has no
    // #theme-toggle, so without it the wipe would have to guess.
    set: function (k, from) {
      var next = has(k) ? k : DEFAULT;
      writePref(KEY, next);
      swap(function () { apply(next); }, from || 'theme-toggle');
      return next;
    },
  };
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

  // Live cross-page sync: localStorage is shared across all same-origin extension pages,
  // and the `storage` event fires in every OTHER document when one of them writes KEY. So
  // changing the accent in the options page (or popup) re-applies here without a reload —
  // fixing an already-open side panel / DevTools panel that used to stay on the old accent.
  // (No echo: the event never fires in the document that made the change.)
  try {
    window.addEventListener('storage', function (e) {
      // Animated here too: this page is repainting for a change made elsewhere, and a
      // silent palette jump reads as a glitch. No pointer here, so it blooms from the centre.
      if (e.key === KEY || e.key === null) swap(function () { apply(read()); }, 'theme-toggle');   // key===null on localStorage.clear()
      if (e.key === TKEY || e.key === null) swap(function () { applyTheme(readTheme()); }, 'theme-toggle');
    });
  } catch (e) { /* no window — not a page context */ }
})();
