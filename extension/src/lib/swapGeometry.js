// The palette swap's geometry: where the wipe blooms from, the ragged front it grows
// as, and the easing both it and the dust ride. Classic script — see accent.js.
(function () {
  var K = window.StencilKit;
  var particleStyle = K.particleStyle;
  // ── Palette swap animation (browser parity: browser/js/ui/motion.js themeSwap) ──
  // The new palette floods out of the CONTROL that changed it, as a growing circle, via
  // the native View Transitions API; without it every colour consumer gets one beat of
  // transition instead (lib/animations/themeSwap.css .theme-swapping). Hand-rolled rather than
  // imported: this file is a pre-paint classic script with no module graph.
  // ONLY user-initiated changes go through it — the boot-time apply must not animate.
  var SWAP_MS = 280;   // browser parity: motion.js THEME_SWAP_MS
  // The origin is the CONTROL that changed the palette — nothing else qualifies, and with
  // no control on screen the circle blooms from the viewport centre. Never the last
  // pointerdown: it is often unrelated (a native <select> fires none at all) and floods
  // the palette out of a corner. Same rule as browser/js/ui/motion.js and desktop's
  // applyTheme ("the cursor is NOT good enough").

  // Centre of an element that is actually ON SCREEN: `visibility: hidden`, `opacity: 0`
  // and an off-screen clone all leave a perfectly good rect behind, and blooming from one
  // puts the wipe in a corner. checkVisibility cannot see an ANCESTOR's `overflow:
  // hidden` either — hence clippedAway, a port of browser motion.js.
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

  // ── Dust in the wipe's wake (browser parity: motion.js swapDustSpecs) ──
  // The growing circle kicks up specks that ignite on its edge and settle just behind it,
  // in the OLD palette's colours — always INSIDE the ring, because during a view
  // transition the page renders through ::view-transition-new(root), clipped to it.
  // Same hash, curve and numbers as the browser (and desktop themeSwapOverlay.hpp).
  // ── The front (browser parity: motion.js swapEdgePolygon ← lib/dustCloud.js edgeJitter) ──
  // The wipe's edge wears the particle style: a polygon ring whose vertices ride the wipe's
  // easing, each pushed off the nominal radius by the style's own recipe. Classic-script
  // twin of dustCloud.js; tests/accent.test.js pins it.
  var EDGE_POINTS = 240;
  var STYLE_WATER = 1, STYLE_FIRE = 2;
  var EDGE_WATER = { waves: 9, amp: 0.028, ripple: 17, rippleAmp: 0.008 };
  var EDGE_FIRE = { tongues: 20, base: 0.05, vary: 0.05, dip: 0.012, jag: 0.006 };
  var fract = function (v) { return v - Math.floor(v); };
  var edgeJitter = function (style, k, points) {
    var t = k / points;
    if (style === STYLE_WATER)
      return EDGE_WATER.amp * Math.sin(2 * Math.PI * EDGE_WATER.waves * t)
        + EDGE_WATER.rippleAmp * Math.sin(2 * Math.PI * EDGE_WATER.ripple * t + 1);
    if (style === STYLE_FIRE) {
      var tongue = Math.floor(t * EDGE_FIRE.tongues), u = fract(t * EDGE_FIRE.tongues);
      var hgt = EDGE_FIRE.base + EDGE_FIRE.vary * dustNoise(tongue, 5);
      return hgt * Math.pow(Math.sin(Math.PI * u), 3) - EDGE_FIRE.dip + EDGE_FIRE.jag * (dustNoise(k, 7) * 2 - 1);
    }
    return 0;
  };
  // The deepest dip inward: the ring's base overshoots by it (coverage still rules) and
  // the wake's grains hug just inside it.
  var edgeDipOf = function (style) {
    return style === STYLE_WATER ? EDGE_WATER.amp + EDGE_WATER.rippleAmp
      : style === STYLE_FIRE ? EDGE_FIRE.dip + EDGE_FIRE.jag : 0;
  };
  var edgeBaseOf = function (style) { return 1 + edgeDipOf(style) + 0.012; };
  // One end state of the clip, in viewport percentages; grow 0 = collapsed at the
  // origin, 1 = the full ring. CSS interpolates the equal-count vertex pairs.
  var edgePolygon = function (x, y, w, h, grow, style) {
    if (!(w > 0 && h > 0)) return '';
    if (style === undefined) style = styleCode();
    var pc = function (v) { return Math.round(v * 1000) / 1000; };
    var base = Math.sqrt(Math.pow(Math.max(x, w - x), 2) + Math.pow(Math.max(y, h - y), 2)) * edgeBaseOf(style);
    var pts = [];
    for (var k = 0; k < EDGE_POINTS; k++) {
      var a = (k / EDGE_POINTS) * 2 * Math.PI;
      var r = grow ? base * (1 + edgeJitter(style, k, EDGE_POINTS)) : 0;
      pts.push(pc(((x + Math.cos(a) * r) / w) * 100) + '% ' + pc(((y + Math.sin(a) * r) / h) * 100) + '%');
    }
    return 'polygon(' + pts.join(', ') + ')';
  };

  var DUST_MOTES = 4500;
  var DUST_LIFE_MS = 340;
  var DUST_MIN_T = 0.06;   // the ring is a point at t=0 — nothing to ride
  var DUST_MAX_T = 0.94;   // …and the last motes still get their whole life
  var dustNoise = function (a, b) {
    var v = Math.sin(a * 127.1 + b * 311.7) * 43758.5453;
    return v - Math.floor(v);
  };
  // The Y of a cubic-bezier at time t, by bisection on the monotonic X — motion.js
  // bezierY, verbatim. Shared by the wipe's curve and the grain's.
  var bezierY = function (t, x1, y1, x2, y2) {
    var lo = 0, hi = 1, u = t, x, i;
    for (i = 0; i < 24; i++) {
      u = 0.5 * (lo + hi);
      x = 3 * (1 - u) * (1 - u) * u * x1 + 3 * (1 - u) * u * u * x2 + u * u * u;
      if (x < t) lo = u; else hi = u;
    }
    return 3 * (1 - u) * (1 - u) * u * y1 + 3 * (1 - u) * u * u * y2 + u * u * u;
  };
  // Where the ring IS at time-fraction t: the wipe's own cubic-bezier(0.4,0.25,0.95,1).
  var dustEase = function (t) { return bezierY(t, 0.4, 0.25, 0.95, 1); };

  var styleCode = function () {
    var s = particleStyle();
    return s === 'water' ? STYLE_WATER : s === 'fire' ? STYLE_FIRE : 0;
  };

  // Published for the scripts after this one (see accent.js).
  K.DUST_LIFE_MS = DUST_LIFE_MS; K.DUST_MAX_T = DUST_MAX_T; K.DUST_MIN_T = DUST_MIN_T;
  K.DUST_MOTES = DUST_MOTES; K.EDGE_POINTS = EDGE_POINTS; K.STYLE_FIRE = STYLE_FIRE;
  K.STYLE_WATER = STYLE_WATER; K.SWAP_MS = SWAP_MS; K.bezierY = bezierY;
  K.dustEase = dustEase; K.dustNoise = dustNoise; K.edgeDipOf = edgeDipOf;
  K.edgeJitter = edgeJitter; K.edgePolygon = edgePolygon; K.fract = fract;
  K.originOf = originOf; K.styleCode = styleCode;
})();
