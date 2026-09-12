// The palette swap's geometry: where the wipe blooms from, the ragged front it grows
// as, and the easing both it and the dust ride. Classic script — see accent.js.
(function () {
  const K = window.StencilKit;
  const particleStyle = K.particleStyle;
  // Browser parity: browser/js/ui/motion.js themeSwap. Only user-initiated changes animate.
  const SWAP_MS = 280;   // browser parity: motion.js THEME_SWAP_MS

  // checkVisibility cannot see an ancestor's `overflow: hidden`; a clipped element leaves
  // a perfectly good rect behind and would bloom the wipe from a corner.
  const clippedAway = function (el, r) {
    if (typeof getComputedStyle !== 'function') return false;
    for (let p = el.parentElement; p; p = p.parentElement) {
      const s = getComputedStyle(p);
      if (s.overflow === 'visible' && s.overflowX === 'visible' && s.overflowY === 'visible') continue;
      const b = p.getBoundingClientRect();
      if (r.right <= b.left || r.left >= b.right || r.bottom <= b.top || r.top >= b.bottom) return true;
    }
    return false;
  };

  const centreOf = function (el) {
    if (!el || !el.getBoundingClientRect) return null;
    const r = el.getBoundingClientRect();
    if (!r.width && !r.height) return null;
    if (el.parentElement && clippedAway(el, r)) return null;
    if (typeof el.checkVisibility === 'function' &&
        !el.checkVisibility({ visibilityProperty: true, opacityProperty: true, contentVisibilityAuto: true }))
      return null;
    const c = { x: r.left + r.width / 2, y: r.top + r.height / 2 };
    if (c.x < 0 || c.y < 0 || c.x > window.innerWidth || c.y > window.innerHeight) return null;
    return c;
  };

  // The origin is the CONTROL that changed the palette, never the last pointerdown. An id
  // scans every element carrying it: a cloned toolbar duplicates ids, and a hidden copy
  // winning getElementById is how the wipe blooms from the wrong place.
  const originOf = function (ref) {
    if (!ref) return null;
    if (typeof ref !== 'string') return centreOf(ref);
    const all = document.querySelectorAll('[id="' + ref + '"]');
    for (let i = 0; i < all.length; i++) {
      const c = centreOf(all[i]);
      if (c) return c;
    }
    return null;
  };

  // The wipe's edge: browser parity with motion.js swapEdgePolygon / dustCloud.js
  // edgeJitter, same numbers as desktop themeSwapOverlay.hpp; tests/accent.test.js pins it.
  const EDGE_POINTS = 240;
  const STYLE_WATER = 1, STYLE_FIRE = 2;
  const EDGE_WATER = { waves: 9, amp: 0.028, ripple: 17, rippleAmp: 0.008 };
  const EDGE_FIRE = { tongues: 20, base: 0.05, vary: 0.05, dip: 0.012, jag: 0.006 };
  const fract = function (v) { return v - Math.floor(v); };
  const edgeJitter = function (style, k, points) {
    const t = k / points;
    if (style === STYLE_WATER)
      return EDGE_WATER.amp * Math.sin(2 * Math.PI * EDGE_WATER.waves * t)
        + EDGE_WATER.rippleAmp * Math.sin(2 * Math.PI * EDGE_WATER.ripple * t + 1);
    if (style === STYLE_FIRE) {
      const tongue = Math.floor(t * EDGE_FIRE.tongues), u = fract(t * EDGE_FIRE.tongues);
      const hgt = EDGE_FIRE.base + EDGE_FIRE.vary * dustNoise(tongue, 5);
      return hgt * Math.pow(Math.sin(Math.PI * u), 3) - EDGE_FIRE.dip + EDGE_FIRE.jag * (dustNoise(k, 7) * 2 - 1);
    }
    return 0;
  };
  // The deepest dip inward: the ring's base overshoots by it so coverage still rules.
  const edgeDipOf = function (style) {
    return style === STYLE_WATER ? EDGE_WATER.amp + EDGE_WATER.rippleAmp
      : style === STYLE_FIRE ? EDGE_FIRE.dip + EDGE_FIRE.jag : 0;
  };
  const edgeBaseOf = function (style) { return 1 + edgeDipOf(style) + 0.012; };
  // grow 0 = collapsed at the origin, 1 = the full ring; CSS interpolates the vertex pairs.
  const edgePolygon = function (x, y, w, h, grow, style) {
    if (!(w > 0 && h > 0)) return '';
    if (style === undefined) style = styleCode();
    const pc = function (v) { return Math.round(v * 1000) / 1000; };
    const base = Math.sqrt(Math.pow(Math.max(x, w - x), 2) + Math.pow(Math.max(y, h - y), 2)) * edgeBaseOf(style);
    const pts = [];
    for (let k = 0; k < EDGE_POINTS; k++) {
      const a = (k / EDGE_POINTS) * 2 * Math.PI;
      const r = grow ? base * (1 + edgeJitter(style, k, EDGE_POINTS)) : 0;
      pts.push(pc(((x + Math.cos(a) * r) / w) * 100) + '% ' + pc(((y + Math.sin(a) * r) / h) * 100) + '%');
    }
    return 'polygon(' + pts.join(', ') + ')';
  };

  const DUST_MOTES = 4500;
  const DUST_LIFE_MS = 340;
  const DUST_MIN_T = 0.06;   // the ring is a point at t=0 — nothing to ride
  const DUST_MAX_T = 0.94;   // …and the last motes still get their whole life
  const dustNoise = function (a, b) {
    const v = Math.sin(a * 127.1 + b * 311.7) * 43758.5453;
    return v - Math.floor(v);
  };
  // motion.js bezierY, verbatim.
  const bezierY = function (t, x1, y1, x2, y2) {
    let lo = 0, hi = 1, u = t, x, i;
    for (i = 0; i < 24; i++) {
      u = 0.5 * (lo + hi);
      x = 3 * (1 - u) * (1 - u) * u * x1 + 3 * (1 - u) * u * u * x2 + u * u * u;
      if (x < t) lo = u; else hi = u;
    }
    return 3 * (1 - u) * (1 - u) * u * y1 + 3 * (1 - u) * u * u * y2 + u * u * u;
  };
  // The wipe's own curve.
  const dustEase = function (t) { return bezierY(t, 0.4, 0.25, 0.95, 1); };

  const styleCode = function () {
    const s = particleStyle();
    return s === 'water' ? STYLE_WATER : s === 'fire' ? STYLE_FIRE : 0;
  };

  K.DUST_LIFE_MS = DUST_LIFE_MS; K.DUST_MAX_T = DUST_MAX_T; K.DUST_MIN_T = DUST_MIN_T;
  K.DUST_MOTES = DUST_MOTES; K.EDGE_POINTS = EDGE_POINTS; K.STYLE_FIRE = STYLE_FIRE;
  K.STYLE_WATER = STYLE_WATER; K.SWAP_MS = SWAP_MS; K.bezierY = bezierY;
  K.dustEase = dustEase; K.dustNoise = dustNoise; K.edgeDipOf = edgeDipOf;
  K.edgeJitter = edgeJitter; K.edgePolygon = edgePolygon; K.fract = fract;
  K.originOf = originOf; K.styleCode = styleCode;
})();
