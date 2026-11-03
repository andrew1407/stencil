// The palette swap per window, as the desktop runs one overlay per dialog: each open dialog, menu and list
// takes a view-transition name of its own and its own reveal, from the control used inside it, else the
// page's control where that lies over it, else its own centre. Browser motion/surface/themeSwapWindows.js,
// held to it by tests/lib/dust/accentSwap.test.js. Classic script — see accent.js.
(function () {
  const K = window.StencilKit;
  const SWAP_WINDOWS = ['.dialog-back .dialog', '.confirm-overlay .confirm-box', '.accent-dd-menu:not([hidden])',
    '.action-menu:not([hidden])'].join(', ');
  const NAME = 'swap-win-', MIN_PX = 8;
  const originIn = function (el, r, page, doc) {
    const focus = doc.activeElement;
    const f = focus && focus !== el && el.contains(focus) ? focus.getBoundingClientRect() : null;
    if (f && f.width) return { x: f.left + f.width / 2 - r.left, y: f.top + f.height / 2 - r.top };
    if (page && page.x >= r.left && page.x <= r.right && page.y >= r.top && page.y <= r.bottom)
      return { x: page.x - r.left, y: page.y - r.top };
    return { x: r.width / 2, y: r.height / 2 };
  };
  const nameSwapWindows = function (page, mode, ms, ease, selector, doc, fromTop) {
    selector = selector || SWAP_WINDOWS;
    doc = doc || document;
    const named = [], css = [];
    if (!doc || typeof doc.querySelectorAll !== 'function') return { css: '', release: function () {} };
    const all = doc.querySelectorAll(selector);
    for (let i = 0; i < all.length; i++) {
      const el = all[i], r = el.getBoundingClientRect ? el.getBoundingClientRect() : null;
      if (!(r && r.width >= MIN_PX && r.height >= MIN_PX) || !el.style) continue;
      const name = NAME + named.length, frames = name + '-reveal', o = originIn(el, r, page, doc);
      el.style.viewTransitionName = name;
      named.push(el);
      let anim = frames + ' ' + ms + 'ms ' + ease + ' both';
      if (mode === 'slide') {
        css.push(K.swapGenieKeyframes(o.x, o.y, r.width, r.height, !!fromTop).replace('themeSwapGenie', frames));
        anim = frames + ' ' + ms + 'ms linear both';
      } else {
        const wipe = mode === 'transition';
        const from = wipe ? 'inset(0 100% 0 0)' : K.edgePolygon(o.x, o.y, r.width, r.height, 0);
        const to = wipe ? 'inset(0 0 0 0)' : K.edgePolygon(o.x, o.y, r.width, r.height, 1);
        css.push('@keyframes ' + frames + ' { from { clip-path: ' + from + '; } to { clip-path: ' + to + '; } }');
      }
      // Its old picture holds under the new one, which its own clip grows over it.
      css.push('::view-transition-group(' + name + ') { animation: none; }',
               '::view-transition-old(' + name + ') { animation: none; mix-blend-mode: normal; }',
               '::view-transition-new(' + name + ') { animation: ' + anim + '; mix-blend-mode: normal; }');
    }
    return { css: css.join('\n'), release: function () { named.forEach(function (el) { el.style.viewTransitionName = ''; }); } };
  };
  // Names and writes every open window's reveal for the swap about to start; returns the release.
  let sheet = null;
  const swapWindows = function (page, fromTop) {
    const mode = K.readMotion();
    const wins = nameSwapWindows(page, mode, mode === 'slide' ? K.SWAP_GENIE_MS : K.SWAP_MS, 'cubic-bezier(0.4, 0.25, 0.95, 1)',
      null, null, fromTop);
    if (typeof document.createElement === 'function' && document.head) {
      if (!sheet || !sheet.isConnected) { sheet = document.createElement('style'); document.head.appendChild(sheet); }
      sheet.textContent = wins.css;
    }
    return wins.release;
  };
  K.SWAP_WINDOWS = SWAP_WINDOWS; K.nameSwapWindows = nameSwapWindows; K.swapWindows = swapWindows;
})();
