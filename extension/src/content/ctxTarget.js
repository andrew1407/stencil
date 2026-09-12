// Right-click probe on <all_urls> (also injected into open tabs by the SW): resolves what
// Stencil can grab under the cursor and messages the SW. Real <img>/<svg><image> return null
// (native context covers). Classic script — no import; the guard stops a double injection.
(() => {
  if (window.__stencilCtxProbe) return;
  window.__stencilCtxProbe = true;

  // mirror of lib/messages.js (classic content script — can't import)
  const MSG = { WAKE: 'stencil-wake', CTX: 'stencil-ctx' };

  // Wake the lazy SW so the menu exists before the first right-click.
  try {
    chrome.runtime.sendMessage({ type: MSG.WAKE }, () => void chrome.runtime.lastError);
  } catch {
    /* ignore */
  }

  // Some players strip <video poster> once playback starts: stamp it early on the element,
  // where this probe and the popup scan (same isolated world) can recover it.
  const STAMP = '__stencilPoster';
  const rememberPoster = (v) => {
    if (v && v.tagName === 'VIDEO' && v.poster) {
      try {
        v[STAMP] = v.poster;
      } catch {
        /* frozen element — ignore */
      }
    }
  };
  const posterOf = (v) => (v && (v.poster || v[STAMP])) || '';
  const snapshotPosters = () => {
    try {
      document.querySelectorAll('video').forEach(rememberPoster);
    } catch {
      /* ignore */
    }
  };
  snapshotPosters();
  try {
    new MutationObserver((muts) => {
      for (const m of muts) {
        if (m.type === 'attributes') {
          rememberPoster(m.target);
        } else {
          for (const n of m.addedNodes || []) {
            if (n.tagName === 'VIDEO') rememberPoster(n);
            else if (n.querySelectorAll) n.querySelectorAll('video').forEach(rememberPoster);
          }
        }
      }
    }).observe(document.documentElement, {
      subtree: true, childList: true, attributes: true, attributeFilter: ['poster']
    });
  } catch {
    /* observer unsupported — live attribute still works */
  }

  const firstCssImageUrl = (bg) => {
    if (!bg || bg === 'none') return null;
    const re = /url\((['"]?)(.*?)\1\)/g;
    let m;
    while ((m = re.exec(bg))) {
      const u = m[2];
      if (u) return u;
    }
    return null;
  };

  const cssImageUrlOf = (el) => {
    for (const pseudo of [null, '::before', '::after']) {
      const u = firstCssImageUrl(getComputedStyle(el, pseudo).backgroundImage);
      if (u) return u;
    }
    return null;
  };

  // Cap the captured frame's longest side: it rides in the editor launch URL as a
  // data URL, and an un-capped 4K frame overflows Chrome's URL limit (about:blank).
  const FRAME_MAX_SIDE = 1920;

  // On its POSTER (never played, or no decoded data) drawImage() yields frame 0, commonly
  // black — the caller should use the poster instead.
  const showingPoster = (v) => (v.paused && !v.currentTime) || v.readyState < 2;

  // null when the poster is showing or the canvas is tainted; the caller then falls back to
  // the poster, then a tab screenshot.
  const captureVideoFrame = (video) => {
    if (!video) return null;
    const vw = video.videoWidth, vh = video.videoHeight;
    if (!vw || !vh || showingPoster(video)) return null;
    const s = Math.min(1, FRAME_MAX_SIDE / Math.max(vw, vh));
    const w = Math.max(1, Math.round(vw * s)), h = Math.max(1, Math.round(vh * s));
    try {
      const c = document.createElement('canvas');
      c.width = w; c.height = h;
      c.getContext('2d').drawImage(video, 0, 0, w, h);
      return c.toDataURL('image/jpeg', 0.92);
    } catch {
      return null; // tainted — caller falls back to a tab screenshot
    }
  };

  // Players lay a controls overlay over the <video>, so closest() misses it: geometric hit-test.
  const videoAt = (start, x, y) => {
    const direct = start.closest && start.closest('video');
    if (direct) return direct;
    if (x != null) {
      // Smallest <video> containing the cursor — never a sibling video sharing the wrapper.
      let best = null, bestArea = Infinity;
      for (const v of document.querySelectorAll('video')) {
        const r = v.getBoundingClientRect();
        if (r.width && r.height && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
          const a = r.width * r.height;
          if (a < bestArea) { bestArea = a; best = v; }
        }
      }
      if (best) return best;
      if (typeof document.elementsFromPoint === 'function')
        for (const el of document.elementsFromPoint(x, y)) if (el.tagName === 'VIDEO') return el;
    }
    return null;
  };

  // The image under the cursor POINT, for photos buried beneath click-catcher overlays.
  const imageUnderPoint = (x, y) => {
    if (x == null || typeof document.elementsFromPoint !== 'function') return null;
    for (const el of document.elementsFromPoint(x, y)) {
      // <svg><image>: el.src is an SVGAnimatedString, not a URL — read the href attribute.
      const u = el.tagName === 'IMG' ? (el.currentSrc || el.src)
        : el.tagName === 'IMAGE' ? (el.getAttribute('href') || el.getAttribute('xlink:href'))
          : null;
      if (u) {
        try {
          return new URL(u, location.href).href;
        } catch {
          return u;
        }
      }
      const bg = cssImageUrlOf(el);
      if (bg) {
        try {
          return new URL(bg, location.href).href;
        } catch {
          return bg;
        }
      }
    }
    return null;
  };

  // Restricted to image extensions so a normal page link never reveals the menu.
  const IMG_LINK_EXT = /\.(avif|bmp|gif|jpe?g|png|svg|webp|ico|tiff?)(?:[?#]|$)/i;
  const imageLinkUrl = (start) => {
    const a = start && start.closest && start.closest('a[href]');
    if (!a) return null;
    const raw = a.getAttribute('href');
    if (!raw) return null;
    try {
      const abs = new URL(raw, location.href).href;
      return IMG_LINK_EXT.test(abs) ? abs : null;
    } catch {
      return null;
    }
  };

  const bgUrlFor = (start) => {
    for (let node = start; node && node.nodeType === 1; node = node.parentElement) {
      const u = cssImageUrlOf(node);
      if (u) {
        try {
          return new URL(u, location.href).href;
        } catch {
          return null;
        }
      }
    }
    return null;
  };

  // TOP-window coordinates (each same-origin iframe's offset added) so a tab screenshot crops
  // correctly; a cross-origin ancestor throws and stops the walk.
  const topRect = (el) => {
    const r = el.getBoundingClientRect();
    let x = r.x, y = r.y, win = el.ownerDocument.defaultView;
    try {
      while (win && win.frameElement) {
        const fr = win.frameElement.getBoundingClientRect();
        x += fr.x; y += fr.y;
        win = win.parent;
      }
    } catch {
      /* cross-origin ancestor — use what we have */
    }
    return { x, y, width: r.width, height: r.height };
  };

  // `light` skips the video FRAME capture: hover priming only needs WHICH menu group applies,
  // and the click handler re-captures in-page anyway.
  const resolveTarget = (start, x, y, light) => {
    if (!start) return null;
    // A real <img>: the native 'image' context builds the menu, so report ONLY `imgUrl` (never
    // `url`, which would reveal the background group) — enough to label Pin ↔ Unpin.
    const imgEl = start.closest && (start.closest('img') || start.closest('image'));
    if (imgEl) {
      const raw = imgEl.tagName === 'IMAGE'
        ? (imgEl.getAttribute('href') || imgEl.getAttribute('xlink:href'))
        : (imgEl.currentSrc || imgEl.src);
      let imgUrl = '';
      if (raw) { try { imgUrl = new URL(raw, location.href).href; } catch { imgUrl = raw; } }
      return imgUrl ? { imgUrl } : null;
    }
    const video = videoAt(start, x, y);
    if (video) {
      // posterOf, not the live attribute: a player may strip it after playback.
      let poster = '';
      const rawPoster = posterOf(video);
      if (rawPoster) {
        try {
          poster = new URL(rawPoster, location.href).href;
        } catch {
          poster = rawPoster;
        }
      }
      const frame = light ? null : captureVideoFrame(video);
      // http(s) only — the openable source a pin keys on, so the SW can label Pin ↔ Unpin.
      const rawMedia = video.currentSrc || video.src || '';
      let videoUrl = '';
      if (rawMedia.startsWith('http:') || rawMedia.startsWith('https:')) {
        try { videoUrl = new URL(rawMedia, location.href).href; } catch { videoUrl = rawMedia; }
      }
      // `video` makes the click handler prefer this frame over info.srcUrl (the media file);
      // `posterShown` tells it to use the poster, not a screenshot, when no frame read.
      if (frame) return { url: frame, video: true, poster, videoUrl };
      return { video: true, rect: topRect(video), dpr: window.devicePixelRatio || 1, poster, posterShown: showingPoster(video), videoUrl };
    }
    const bg = bgUrlFor(start);
    if (bg) return { url: bg };
    const under = imageUnderPoint(x, y);
    if (under) return { url: under };
    const link = imageLinkUrl(start);
    return link ? { url: link } : null;
  };

  // The SW may be asleep / the page navigating — a failed send is fine.
  const send = (data, x, y) => {
    try {
      chrome.runtime.sendMessage({ type: MSG.CTX, data, point: { x, y } });
    } catch {
      /* ignore */
    }
  };

  // Priming: an update sent from `contextmenu` races Chrome's menu render (and loses when the
  // MV3 worker must wake first), so resolve on HOVER too — by right-click time the worker
  // is warm and the right group is already revealed.
  const PRIME_MS = 150;
  let primeAt = 0;
  let primedEl = null;
  let primedKey = '';
  // Dedupe key: hovering ten tiles of the same background sends one message.
  const keyOf = (d) => (!d ? '' : `${d.video ? 'v' : 'i'}|${d.url || ''}|${d.imgUrl || ''}|${d.poster || ''}`);
  const prime = (e) => {
    const now = Date.now();
    if (e.target === primedEl && now - primeAt < 1000) return;   // same element, nothing moved
    if (now - primeAt < PRIME_MS) return;                        // throttle a fast sweep
    primeAt = now;
    primedEl = e.target;
    let data = null;
    try { data = resolveTarget(e.target, e.clientX, e.clientY, true); } catch { data = null; }
    const key = keyOf(data);
    if (key === primedKey) return;                               // nothing changed for the menu
    primedKey = key;
    send(data, e.clientX, e.clientY);
  };
  document.addEventListener('pointerover', prime, true);
  document.addEventListener('pointermove', prime, true);
  // A right-BUTTON press beats `contextmenu` on every platform — one last chance to prime.
  document.addEventListener('mousedown', (e) => { if (e.button === 2) prime(e); }, true);

  // The authoritative resolve: frame capture included, with the exact point for the SW's re-capture.
  document.addEventListener('contextmenu', (e) => {
    let data = null;
    try {
      data = resolveTarget(e.target, e.clientX, e.clientY);
    } catch {
      data = null;
    }
    primedKey = keyOf(data);
    primedEl = e.target;
    primeAt = Date.now();
    send(data, e.clientX, e.clientY);
  }, true);
})();
