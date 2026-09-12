// The wake itself: how many grains a swap spawns, where each starts, and the canvas
// stage that paints them. Classic script — see accent.js.
(function () {
  const K = window.StencilKit;
  const DUST_ALPHA_LEVELS = K.DUST_ALPHA_LEVELS, DUST_LIFE_MS = K.DUST_LIFE_MS, DUST_MAX_T = K.DUST_MAX_T, DUST_MIN_T = K.DUST_MIN_T, DUST_MOTES = K.DUST_MOTES;
  const SWAP_MS = K.SWAP_MS, dustEase = K.dustEase, dustMix = K.dustMix, dustNoise = K.dustNoise, edgeDipOf = K.edgeDipOf;
  const fillGrains = K.fillGrains, grainAt = K.grainAt, grainShape = K.grainShape, headingOf = K.headingOf, paletteCss = K.paletteCss;
  const particleStyle = K.particleStyle, resolveColour = K.resolveColour, stopOfTint = K.stopOfTint, styleCode = K.styleCode, styleFrame = K.styleFrame;
  const tintOf = K.tintOf;
  const dustSpecs = function (x, y, w, h, style) {
    const R = Math.sqrt(Math.pow(Math.max(x, w - x), 2) + Math.pow(Math.max(y, h - y), 2));
    const specs = [];
    if (!(R > 0)) return specs;
    const dip = edgeDipOf(style === undefined ? styleCode() : style);
    for (let i = 0; i < DUST_MOTES; i++) {
      const n = dustNoise(i, 3), m = dustNoise(i + 57, 11), q = dustNoise(i + 13, 29);
      const angle = n * 2 * Math.PI;
      const u = DUST_MIN_T + m * (DUST_MAX_T - DUST_MIN_T);
      // Just behind the torn edge's deepest tooth, so grains and clip read as one front.
      const r = dustEase(u) * R * (1 - dip) - q * 6;
      if (r <= 0) continue;
      const cx = x + Math.cos(angle) * r, cy = y + Math.sin(angle) * r;
      if (cx < -16 || cy < -16 || cx > w + 16 || cy > h + 16) continue;
      const size = 2.5 + n * 3.5;
      const d = 8 + q * 14;
      const dx = Math.round(Math.cos(angle) * d + (m - 0.5) * 14);
      const dy = Math.round(Math.sin(angle) * d + (0.5 - q) * 14);
      specs.push({
        cx: cx, cy: cy, size: size,
        dx: dx,
        dy: dy,
        delay: Math.round(u * SWAP_MS),
        alpha: 0.75 + q * 0.25,
        // Every fourth grain is the departing accent, so an accent cycle still reads
        // over an unchanged background.
        accent: i % 4 === 0,
        w: dustNoise(i + 71, 13),
        len: Math.sqrt(dx * dx + dy * dy),
      });
    }
    return specs;
  };
  // Read BEFORE the palette flips: by the time a mote is on screen the variables
  // already mean the NEW theme.
  const dustPaint = function () {
    try {
      const s = getComputedStyle(document.documentElement);
      const v = function (name) { return (s.getPropertyValue(name) || '').replace(/^\s+|\s+$/g, ''); };
      if (!v('--accent')) return null;
      const palette = paletteCss();
      for (let pi = 0; pi < palette.length; pi++) palette[pi] = resolveColour(palette[pi]);
      return { palette: palette };
    } catch (e) { return null; }
  };
  // The canvas silently keeps what it had for anything it cannot parse.
  const canvasColour = function (ctx, c, fallback) {
    ctx.fillStyle = fallback;
    ctx.fillStyle = c;
    return ctx.fillStyle;
  };

  // ONE canvas, never a div per grain: batched by colour and alpha step into a handful
  // of fills a frame.
  const spawnDust = function (px, paint) {
    try {
      if (!px || !paint || typeof document === 'undefined' || !document.body || !document.body.appendChild) return;
      // 'slide' keeps the wipe and drops its grain.
      if (typeof requestAnimationFrame !== 'function' || !particleStyle()) return;
      const root = document.documentElement;
      // The newest swap owns the dust.
      if (root._swapDustStop) root._swapDustStop();
      const style = styleCode();
      const specs = dustSpecs(px.x, px.y, px.w, px.h, style);
      if (!specs.length) return;
      const stage = document.createElement('canvas');
      const ctx = stage.getContext && stage.getContext('2d');
      if (!ctx) return;
      stage.className = 'swap-dust';
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      stage.width = Math.round(px.w * dpr);
      stage.height = Math.round(px.h * dpr);
      stage.style.width = px.w + 'px';
      stage.style.height = px.h + 'px';
      ctx.scale(dpr, dpr);
      // One fillStyle per palette stop: a handful of fills, not a thousand switches.
      const runs = []; let k;
      for (k = 0; k < paint.palette.length; k++) runs.push({ colour: canvasColour(ctx, paint.palette[k], '#888') });
      document.body.appendChild(stage);
      // [x, y, r, shape, heading] per grain, bucketed by alpha step and reused every frame.
      const lvl = [], lvlN = new Int32Array(DUST_ALPHA_LEVELS); let l;
      for (l = 0; l < DUST_ALPHA_LEVELS; l++) lvl.push(new Float32Array(specs.length * 5));
      const at = { x: 0, y: 0, r: 0, alpha: 0 };
      const sf = { sx: 0, sy: 0, scale: 1, glow: 1, mix: 0 };
      const poly = [];
      const stopOf = new Int8Array(specs.length);
      const fx = new Float32Array(specs.length * 4);
      const shapes = new Int8Array(specs.length), heads = new Float32Array(specs.length);
      const tints = new Int8Array(specs.length);
      for (k = 0; k < specs.length; k++) {
        shapes[k] = grainShape(style, specs[k].w);
        heads[k] = headingOf(specs[k].dx, specs[k].dy, false);
        tints[k] = tintOf(specs[k].w);
      }
      const total = SWAP_MS + DUST_LIFE_MS;
      const started = performance.now();
      let raf = 0;
      const stop = function () {
        if (typeof cancelAnimationFrame === 'function') cancelAnimationFrame(raf);
        clearTimeout(root._swapDustTimer);
        stage.remove();
        if (root._swapDustStop === stop) { root._swapDustStop = null; root._swapDustTimer = null; }
      };
      const frame = function (now) {
        const ms = now - started;
        if (ms >= total) { stop(); return; }
        ctx.clearRect(0, 0, px.w, px.h);
        for (let fi = 0; fi < specs.length; fi++) {
          const fs = specs[fi];
          const fp = (ms - fs.delay) / DUST_LIFE_MS;
          if (fp <= 0 || fp >= 1) { stopOf[fi] = -1; continue; }
          grainAt(fs, fp, at);
          styleFrame(style, fp, fp, fs.w, fs.len, ms, sf);
          stopOf[fi] = stopOfTint(style ? sf.mix : dustMix(fs.w, fs.accent), tints[fi]);
          fx[fi * 4] = at.x + sf.sx; fx[fi * 4 + 1] = at.y + sf.sy;
          fx[fi * 4 + 2] = at.r * sf.scale; fx[fi * 4 + 3] = at.alpha * sf.glow;
        }
        for (let ri = 0; ri < runs.length; ri++) {
          lvlN.fill(0);
          let buf, j;
          for (let gi2 = 0; gi2 < specs.length; gi2++) {
            if (stopOf[gi2] !== ri) continue;
            const li = Math.round(fx[gi2 * 4 + 3] * DUST_ALPHA_LEVELS) - 1;
            if (li < 0) continue;
            buf = lvl[li];
            j = lvlN[li]++ * 5;
            buf[j] = fx[gi2 * 4]; buf[j + 1] = fx[gi2 * 4 + 1]; buf[j + 2] = fx[gi2 * 4 + 2];
            buf[j + 3] = shapes[gi2]; buf[j + 4] = heads[gi2];
          }
          ctx.fillStyle = runs[ri].colour;
          for (let li2 = 0; li2 < DUST_ALPHA_LEVELS; li2++) {
            const n = lvlN[li2];
            if (!n) continue;
            ctx.globalAlpha = (li2 + 1) / DUST_ALPHA_LEVELS;
            fillGrains(ctx, lvl[li2], n, poly);
          }
        }
        raf = requestAnimationFrame(frame);
      };
      raf = requestAnimationFrame(frame);
      root._swapDustStop = stop;
      // A paused rAF (a backgrounded tab) would otherwise leave the stage up for good.
      root._swapDustTimer = setTimeout(stop, total + 200);
    } catch (e) { /* decoration only */ }
  };


  K.dustPaint = dustPaint; K.spawnDust = spawnDust;
})();
