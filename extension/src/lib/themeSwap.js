// The palette swap: run `fn` inside a View Transition so the new colours flood out of
// the control that changed them, with the dust wake behind the front. Classic script —
// see accent.js.
(function () {
  var K = window.StencilKit;
  var SWAP_MS = K.SWAP_MS, dustPaint = K.dustPaint, edgePolygon = K.edgePolygon, motionReduced = K.motionReduced, originOf = K.originOf;
  var spawnDust = K.spawnDust;
  var swap = function (fn, originId) {
    var root = document.documentElement;
    // The motion mode's 'none' and the OS preference alike apply the palette outright —
    // the wipe is motion too (browser parity: motion.js themeSwap motionReduced()).
    var reduced = motionReduced();
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
    // `dustAt` keeps the same answer in PIXELS — the dust is seeded off wherever the
    // circle was really painted from, so the wake and the ring can never disagree.
    var dustAt = null;
    var write = function () {
      var w = window.innerWidth, h = window.innerHeight;
      var p = originOf(originId) || { x: w / 2, y: h / 2 };
      dustAt = { x: p.x, y: p.y, w: w, h: h };
      // The radius that reaches the furthest corner — the circle must cover the whole page.
      var r = Math.sqrt(Math.pow(Math.max(p.x, w - p.x), 2) + Math.pow(Math.max(p.y, h - p.y), 2));
      var pc = function (v) { return Math.round(v * 1000) / 1000; };
      var diag = Math.sqrt(w * w + h * h);
      root.style.setProperty('--swap-x', pc((100 * p.x) / w) + '%');
      root.style.setProperty('--swap-y', pc((100 * p.y) / h) + '%');
      root.style.setProperty('--swap-r', pc((100 * Math.SQRT2 * r) / diag) + '%');
      root.style.setProperty('--swap-ms', SWAP_MS + 'ms');
      // The clip the reveal actually plays: the ragged polygon pair (the circle above
      // stays the keyframes' fallback and the geometry record tests read).
      var from = edgePolygon(p.x, p.y, w, h, 0);
      if (from) {
        root.style.setProperty('--swap-clip-from', from);
        root.style.setProperty('--swap-clip-to', edgePolygon(p.x, p.y, w, h, 1));
      }
    };
    write();
    // The OLD palette, read before `fn` flips it — the wake is the paint coming off.
    var paint = dustPaint();
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
    // Dust rides in only once the wipe's own animation is running (`ready`), so a mote's
    // delay and the ring's clock start on the same frame. An engine without `ready` (or
    // a skipped transition) simply gets no dust — the wipe never depends on it, and
    // spawning off `finished` instead would replay the whole wake over a finished swap.
    if (t.ready && t.ready.then) t.ready.then(function () { spawnDust(dustAt, paint); }, function () {});
  };

  // Published for the scripts after this one (see accent.js).
  K.swap = swap;
})();
