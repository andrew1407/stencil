// The palette swap's reveal per interface animation: Transition wipes in from the left edge, Sliding pours
// a theme change out of its control and an accent change down from the top, on the genie's own silhouette. Browser motion/surface/themeSwapShape.js over genie/warp.js
// genieOutline, the same numbers, held to them by tests/lib/dust/accentSwap.test.js. Classic script — see accent.js.
(function () {
  const K = window.StencilKit;
  const WIPE_CLIP_FROM = 'inset(0 100% 0 0)', WIPE_CLIP_TO = 'inset(0 0 0 0)';
  const GENIE_LAG = 0.55, GENIE_PINCH_END = 0.4, GENIE_BEND = 0.85;
  const SWAP_GENIE_MS = 560, SWAP_GENIE_STEPS = 16, SWAP_GENIE_ROWS = 24, SWAP_ICON_PX = 24;
  const SWAP_GENIE_CLASS = 'theme-swap-genie';
  const smooth01 = function (v) { const x = Math.min(1, Math.max(0, v)); return x * x * (3 - 2 * x); };
  const travel = function (v, p) { return smooth01(p * (1 + GENIE_LAG) - GENIE_LAG * (1 - v)); };
  const lerp = function (a, b, t) { return a + (b - a) * t; };
  // The box edge nearest the control: the way a theme change pours.
  const flowSide = function (x, y, w, h) {
    const gaps = [['up', y], ['down', h - y], ['left', x], ['right', w - x]];
    return gaps.reduce(function (a, b) { return b[1] < a[1] ? b : a; })[0];
  };
  // `a` runs toward the icon, `c` across it (browser warp.js canon / uncanon).
  const canon = function (r, side) {
    const right = r.left + r.width, bottom = r.top + r.height;
    if (side === 'down') return { a0: r.top, a1: bottom, c0: r.left, c1: right };
    if (side === 'up') return { a0: -bottom, a1: -r.top, c0: r.left, c1: right };
    if (side === 'right') return { a0: r.left, a1: right, c0: r.top, c1: bottom };
    return { a0: -right, a1: -r.left, c0: r.top, c1: bottom };
  };
  const pointAt = function (a, c, side) {
    if (side === 'down' || side === 'up') return { x: c, y: side === 'up' ? -a : a };
    return { x: side === 'left' ? -a : a, y: c };
  };
  const genieOutline = function (win, icon, side, p, rows) {
    const w = canon(win, side), i = canon(icon, side), len = w.a1 - w.a0, pinch = smooth01(p / GENIE_PINCH_END);
    const one = [], other = [];
    for (let k = 0; k <= rows; k++) {
      const v = k / rows;
      const a = lerp(w.a0 + v * len, i.a0 + v * (i.a1 - i.a0), travel(v, p));
      const f = Math.max(travel(v, p), pinch * GENIE_BEND * smooth01(v));
      one.push(pointAt(a, lerp(w.c0, i.c0, f), side));
      other.push(pointAt(a, lerp(w.c1, i.c1, f), side));
    }
    return one.concat(other.reverse());
  };
  // A theme change pours out of the control; `fromTop` (an accent change) down from the whole top edge.
  const swapGeniePolygon = function (x, y, w, h, p, fromTop) {
    const half = SWAP_ICON_PX / 2;
    const source = fromTop ? { left: 0, top: 0, width: w, height: 1 }
      : { left: x - half, top: y - half, width: SWAP_ICON_PX, height: SWAP_ICON_PX };
    const side = fromTop ? 'up' : flowSide(x, y, w, h);
    const pts = genieOutline({ left: 0, top: 0, width: w, height: h }, source, side, p, SWAP_GENIE_ROWS);
    const pc = function (v, of) { return Math.round((100000 * v) / of) / 1000; };
    return 'polygon(' + pts.map(function (q) { return pc(q.x, w) + '% ' + pc(q.y, h) + '%'; }).join(', ') + ')';
  };
  const swapGenieKeyframes = function (x, y, w, h, fromTop) {
    if (!(w > 0 && h > 0)) return '';
    const frames = [];
    for (let k = 0; k <= SWAP_GENIE_STEPS; k++) {
      // Out-sine for a theme change; an accent's sweep down on an in-out sine (browser themeSwapShape.js swapProgress).
      const t = k / SWAP_GENIE_STEPS, p = fromTop ? (1 + Math.cos(Math.PI * t)) / 2 : 1 - Math.sin((t * Math.PI) / 2);
      frames.push(Math.round(t * 1000) / 10 + '% { clip-path: ' + swapGeniePolygon(x, y, w, h, p, !!fromTop) + '; }');
    }
    return '@keyframes themeSwapGenie { ' + frames.join(' ') + ' }';
  };
  // The reveal the interface animation asks for, onto <html> (lib/animations/themeSwap.css); a page with
  // no sheet to write into keeps the ring.
  let genieSheet = null;
  const writeSwapShape = function (root, px, fromTop) {
    const mode = K.readMotion();
    if (mode === 'transition') {
      root.style.setProperty('--swap-clip-from', WIPE_CLIP_FROM);
      root.style.setProperty('--swap-clip-to', WIPE_CLIP_TO);
    }
    const pour = mode === 'slide' && !!px && typeof document.createElement === 'function' && !!document.head;
    if (root.classList && root.classList.toggle) root.classList.toggle(SWAP_GENIE_CLASS, pour);
    if (!pour) return;
    if (!genieSheet || !genieSheet.isConnected) { genieSheet = document.createElement('style'); document.head.appendChild(genieSheet); }
    genieSheet.textContent = swapGenieKeyframes(px.x, px.y, px.w, px.h, fromTop);
    root.style.setProperty('--swap-ms', SWAP_GENIE_MS + 'ms');
  };
  K.SWAP_GENIE_MS = SWAP_GENIE_MS; K.swapGeniePolygon = swapGeniePolygon; K.swapGenieKeyframes = swapGenieKeyframes; K.writeSwapShape = writeSwapShape;
})();
