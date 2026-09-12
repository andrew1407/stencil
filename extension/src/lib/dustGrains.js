// One grain of the swap's wake: its flight curve, the three particle styles, the shapes
// they are drawn as, and the palette they are tinted from. A hand-copied twin of
// lib/dustCloud.js's grain maths (that module is an ES module this pre-paint classic
// script cannot import); tests/accent.test.js pins the two against each other.
(function () {
  var K = window.StencilKit;
  var STYLE_FIRE = K.STYLE_FIRE, STYLE_WATER = K.STYLE_WATER, bezierY = K.bezierY, fract = K.fract;
  // ── One grain's flight (motion.js swapDustFrame) ──
  // Never a div per grain: at this density that is thousands of composited layers and the
  // swap drops half its frames, so the stage below paints them and the `swapDustMote`
  // keyframes live here — same curve, same three opacity stops, same throw and shrink.
  // Sampled into a table: the bisection is far too dear to run three times per grain per
  // frame; both ends are pinned, since the solver only bisects to within a hair.
  var TAU = Math.PI * 2;
  var GRAIN_STEPS = 256;
  var GRAIN_FLARE = 0.18;   // opacity flares over the first 18% of a life, then falls
  var grainCurve = new Float32Array(GRAIN_STEPS + 1);
  for (var gi = 0; gi <= GRAIN_STEPS; gi++) grainCurve[gi] = bezierY(gi / GRAIN_STEPS, 0.22, 0.55, 0.3, 1);
  grainCurve[0] = 0;
  grainCurve[GRAIN_STEPS] = 1;
  var grainEase = function (t) {
    return grainCurve[Math.min(GRAIN_STEPS, Math.max(0, Math.round(t * GRAIN_STEPS)))];
  };
  // ── The particle styles (lib/dustCloud.js styleFrame, as a classic script) ──
  // A water or fire wake is the same grains nudged, swollen and dimmed per frame, painted
  // from the departing accent palette by each grain's mix. tests/accent.test.js pins this
  // twin to dustCloud.js frame for frame.
  var PALETTE_STOPS = 6;
  var WATER = { sagShare: 0.45, sagMaxPx: 30, swayShare: 0.12, swayMaxPx: 5, swayWaves: [0.8, 1.4], swell: 0.3,
                shimmerDepth: 0.25, shimmerHz: [1.2, 2.2], glistenHz: [0.6, 1.1] };
  var FIRE = { liftShare: 0.6, liftMaxPx: 44, waverShare: 0.08, waverMaxPx: 4, waverWaves: [3, 5], flare: 0.35,
               flickerDepth: 0.55, flickerHz: [9, 14], coolHash: 0.25 };
  var styleFrame = function (style, p, away, w, len, tMs, out) {
    out.sx = 0; out.sy = 0; out.scale = 1; out.glow = 1; out.mix = 0;
    if (style !== STYLE_WATER && style !== STYLE_FIRE) return out;
    var env = Math.sin(Math.PI * p);
    var phase = w * 2 * Math.PI;
    var sec = tMs / 1000;
    var wave = function (range) { return range[0] + (range[1] - range[0]) * w; };
    if (style === STYLE_WATER) {
      out.sy = Math.min(len * WATER.sagShare, WATER.sagMaxPx) * (0.6 + 0.4 * w) * env;
      out.sx = Math.min(len * WATER.swayShare, WATER.swayMaxPx) * env
        * Math.sin(p * wave(WATER.swayWaves) * 2 * Math.PI + phase);
      out.scale = 1 + WATER.swell * env;
      out.glow = 1 - WATER.shimmerDepth * 0.5 * (1 + Math.sin(sec * wave(WATER.shimmerHz) * 2 * Math.PI + phase));
      out.mix = 0.5 + 0.5 * Math.sin(sec * wave(WATER.glistenHz) * 2 * Math.PI + phase);
    } else {
      out.sy = -Math.min(len * FIRE.liftShare, FIRE.liftMaxPx) * (0.5 + 0.5 * w) * env;
      out.sx = Math.min(len * FIRE.waverShare, FIRE.waverMaxPx) * env
        * Math.sin(p * wave(FIRE.waverWaves) * 2 * Math.PI + phase);
      var dim = 0.5 * (1 + Math.sin(sec * wave(FIRE.flickerHz) * 2 * Math.PI + phase));
      out.glow = 1 - FIRE.flickerDepth * dim;
      out.scale = 1 + FIRE.flare * env * (1 - dim);
      out.mix = Math.max(0, Math.min(1, FIRE.coolHash * w + (1 - FIRE.coolHash) * away));
    }
    return out;
  };
  var paletteIndex = function (mix, stops) {
    return Math.max(0, Math.min(stops - 1, Math.round(mix * (stops - 1))));
  };
  // A DUST grain's mix, fixed for its flight (lib/dustCloud.js dustMix): plain grains
  // spread from the main colour to halfway by hash, glints wear the shade.
  var DUST_MIX_SPREAD = 0.5;
  var dustMix = function (w, glint) { return glint ? 1 : (w || 0) * DUST_MIX_SPREAD; };
  // Two grains in three ride that accent ramp; the rest wear a TINT off their own hash —
  // white, two greys, a pale and a deep accent (lib/dustCloud.js tintOf / stopOfTint).
  var TINT_SHARE = 0.34;
  // Two follow the theme (lib/theme/palette.css): a white speck cannot be seen on a pale surface,
  // nor a deep accent one on a dark surface.
  var TINT_CSS = ['var(--dust-ink, #1f1f1f)', '#b4b4b4', '#6e6e6e',
                  'color-mix(in srgb, var(--accent) 55%, #ffffff)',
                  'var(--dust-accent-alt, #442082)'];
  var TINT_STOPS = TINT_CSS.length;
  var tintOf = function (w) {
    var pick = fract((w || 0) * 13.73 + 0.41);
    if (pick >= TINT_SHARE) return -1;
    return Math.min(TINT_STOPS - 1, Math.floor((pick / TINT_SHARE) * TINT_STOPS));
  };
  // A tint is fixed for a grain's flight, so the wake caches it and reads it back per frame.
  var stopOfTint = function (mix, tint) {
    return tint < 0 ? paletteIndex(mix, PALETTE_STOPS) : PALETTE_STOPS + tint;
  };
  // ── Grain shapes (lib/dustCloud.js grainShape / shapePolygon / addGrainPath twin) ──
  // Dust is a round speck; water ovals and wave lines; fire triangles and streaking
  // sparks — each lying along its heading.
  var SHAPE_DISC = 0, SHAPE_OVAL = 1, SHAPE_WAVE = 2, SHAPE_TRIANGLE = 3, SHAPE_STREAK = 4;
  var WATER_WAVE_SHARE = 0.3, FIRE_STREAK_SHARE = 0.4;
  var SHAPES = {
    oval: { rx: 1.45, ry: 0.7 },
    wave: { len: 3.6, amp: 0.42, waves: 1.5, half: 0.28, samples: 9 },
    triangle: { tip: 1.7, base: 0.85, half: 1.0 },
    streak: { head: 1.0, headHalf: 0.42, tail: 2.6, tailHalf: 0.1 },
  };
  var grainShape = function (style, w) {
    var pick = fract((w || 0) * 7.31 + 0.17);
    if (style === STYLE_WATER) return pick < WATER_WAVE_SHARE ? SHAPE_WAVE : SHAPE_OVAL;
    if (style === STYLE_FIRE) return pick < FIRE_STREAK_SHARE ? SHAPE_STREAK : SHAPE_TRIANGLE;
    return SHAPE_DISC;
  };
  var headingOf = function (dx, dy, fromFar) { return Math.atan2(dy, dx) + (fromFar ? Math.PI : 0); };
  var shapePolygon = function (shape, x, y, r, a, out) {
    out.length = 0;
    var c = Math.cos(a), s = Math.sin(a);
    var put = function (u, v) { out.push(x + u * c - v * s, y + u * s + v * c); };
    var t, i;
    if (shape === SHAPE_TRIANGLE) {
      t = SHAPES.triangle;
      put(t.tip * r, 0); put(-t.base * r, t.half * r); put(-t.base * r, -t.half * r);
    } else if (shape === SHAPE_STREAK) {
      t = SHAPES.streak;
      put(t.head * r, t.headHalf * r); put(-t.tail * r, t.tailHalf * r);
      put(-t.tail * r, -t.tailHalf * r); put(t.head * r, -t.headHalf * r);
    } else if (shape === SHAPE_WAVE) {
      t = SHAPES.wave;
      var rim = function (idx, side) {
        var kk = idx / (t.samples - 1);
        put((kk - 0.5) * t.len * r, Math.sin(kk * t.waves * 2 * Math.PI) * t.amp * r + side * t.half * r);
      };
      for (i = 0; i < t.samples; i++) rim(i, 1);
      for (i = t.samples - 1; i >= 0; i--) rim(i, -1);
    }
    return out;
  };
  // A path only so long (lib/dustCloud.js FILL_CHUNK): the engine's cost per grain climbs
  // with the path's length, so every batch is filled in chunks of this many grains.
  var FILL_CHUNK = 32;
  var fillGrains = function (ctx, b, n, poly) {
    for (var i = 0; i < n; i += FILL_CHUNK) {
      var end = Math.min(n, i + FILL_CHUNK);
      ctx.beginPath();
      for (var j = i; j < end; j++) addGrainPath(ctx, b[j * 5 + 3], b[j * 5], b[j * 5 + 1], b[j * 5 + 2], b[j * 5 + 4], poly);
      ctx.fill();
    }
  };
  var addGrainPath = function (ctx, shape, x, y, r, a, scratch) {
    if (shape === SHAPE_DISC) { ctx.moveTo(x + r, y); ctx.arc(x, y, r, 0, TAU); return; }
    if (shape === SHAPE_OVAL) {
      var rx = SHAPES.oval.rx * r, ry = SHAPES.oval.ry * r;
      ctx.moveTo(x + Math.cos(a) * rx, y + Math.sin(a) * rx);
      ctx.ellipse(x, y, rx, ry, a, 0, TAU);
      return;
    }
    var pts = shapePolygon(shape, x, y, r, a, scratch);
    ctx.moveTo(pts[0], pts[1]);
    for (var i = 2; i < pts.length; i += 2) ctx.lineTo(pts[i], pts[i + 1]);
    ctx.closePath();
  };

  // A CSS colour (a var(), a color-mix()) as the canvas will take it: the computed colour
  // of a probe span wearing it. Unchanged when the page cannot compute it.
  var resolveColour = function (css) {
    try {
      var span = document.createElement('span');
      span.style.color = css;
      document.body.appendChild(span);
      var got = typeof getComputedStyle === 'function' ? getComputedStyle(span).color : '';
      span.remove();
      return got || css;
    } catch (e) { return css; }
  };
  // The palette a styled wake is painted in (lib/dustCloud.js paletteCss): the accent →
  // shade ramp, then the tints.
  var paletteCss = function () {
    var out = [], i;
    for (i = 0; i < PALETTE_STOPS; i++)
      out.push('color-mix(in srgb, var(--accent) ' + Math.round(100 - (100 * i) / (PALETTE_STOPS - 1)) + '%, var(--accent-2))');
    for (i = 0; i < TINT_STOPS; i++) out.push(TINT_CSS[i]);
    return out;
  };

  // Where grain `s` is at life-fraction `p`, how big and how bright. Writes into `out`:
  // this runs once per grain per frame.
  var grainAt = function (s, p, out) {
    var e = grainEase(p);
    var o = p < GRAIN_FLARE ? grainEase(p / GRAIN_FLARE)
                            : 1 - grainEase((p - GRAIN_FLARE) / (1 - GRAIN_FLARE));
    out.x = s.cx + s.dx * e;
    out.y = s.cy + s.dy * e;
    out.r = (s.size / 2) * (1 - 0.7 * e);
    out.alpha = s.alpha * o;
  };
  // How many alpha steps a fading grain is drawn in: the stage batches every grain of
  // one colour AND one step into a single fill (browser motion.js DUST_ALPHA_LEVELS).
  var DUST_ALPHA_LEVELS = 8;

  // Published for the scripts after this one (see accent.js).
  K.DUST_ALPHA_LEVELS = DUST_ALPHA_LEVELS; K.dustMix = dustMix; K.fillGrains = fillGrains;
  K.grainAt = grainAt; K.grainShape = grainShape; K.headingOf = headingOf;
  K.paletteCss = paletteCss; K.resolveColour = resolveColour; K.shapePolygon = shapePolygon;
  K.stopOfTint = stopOfTint; K.styleFrame = styleFrame; K.tintOf = tintOf;
})();
