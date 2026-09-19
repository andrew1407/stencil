// The palette swap: `fn` runs inside a View Transition so the new colours flood out of
// the control that changed them. Classic script — see accent.js.
(function () {
  const K = window.StencilKit;
  const SWAP_MS = K.SWAP_MS, dustPaint = K.dustPaint, edgePolygon = K.edgePolygon, motionReduced = K.motionReduced, originOf = K.originOf;
  const spawnDust = K.spawnDust;
  const swap = function (fn, originId) {
    const root = document.documentElement;
    // The wipe is motion too (browser parity: motion.js themeSwap motionReduced()).
    const reduced = motionReduced();
    if (reduced || typeof document.startViewTransition !== 'function') {
      if (!root || !root.classList) { fn(); return; }
      root.classList.add('theme-swapping');
      clearTimeout(root._themeSwapTimer);
      fn();
      root._themeSwapTimer = setTimeout(function () { root.classList.remove('theme-swapping'); }, SWAP_MS);
      return;
    }
    // Handed to the declarative keyframes in lib/animations/themeSwap.css: scripting from
    // ready.then() races the transition's teardown. Percentages, never pixels (device-px halving).
    let dustAt = null;
    const write = function () {
      const w = window.innerWidth, h = window.innerHeight;
      const p = originOf(originId) || { x: w / 2, y: h / 2 };
      dustAt = { x: p.x, y: p.y, w: w, h: h };
      // The radius that reaches the furthest corner.
      const r = Math.sqrt(Math.pow(Math.max(p.x, w - p.x), 2) + Math.pow(Math.max(p.y, h - p.y), 2));
      const pc = function (v) { return Math.round(v * 1000) / 1000; };
      const diag = Math.sqrt(w * w + h * h);
      root.style.setProperty('--swap-x', pc((100 * p.x) / w) + '%');
      root.style.setProperty('--swap-y', pc((100 * p.y) / h) + '%');
      root.style.setProperty('--swap-r', pc((100 * Math.SQRT2 * r) / diag) + '%');
      root.style.setProperty('--swap-ms', SWAP_MS + 'ms');
      // The circle above stays the keyframes' fallback; the ragged polygon pair is what plays.
      const from = edgePolygon(p.x, p.y, w, h, 0);
      if (from) {
        root.style.setProperty('--swap-clip-from', from);
        root.style.setProperty('--swap-clip-to', edgePolygon(p.x, p.y, w, h, 1));
      }
    };
    write();
    // The OLD palette, read before `fn` flips it — the wake is the paint coming off.
    const paint = dustPaint();
    const t = document.startViewTransition(function () {
      // Transitions off while the snapshot is captured, or it records mid-ease colours.
      root.classList.add('theme-instant');
      fn();
      // The control can move as the palette lands, so the origin is re-asked.
      write();
    });
    const settle = function () { root.classList.remove('theme-instant'); };
    t.finished.then(settle, settle);
    // Off `ready`, so a mote's delay and the ring's clock start on the same frame; an
    // engine without `ready` simply gets no dust.
    if (t.ready && t.ready.then) t.ready.then(function () { spawnDust(dustAt, paint); }, function () {});
  };

  K.swap = swap;
})();
