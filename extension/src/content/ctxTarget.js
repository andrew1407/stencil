// ── Right-click probe (content script) ───────────────────────────────────────
// On <all_urls> (also injected into open tabs by the SW). On contextmenu, resolves
// what Stencil can grab under the cursor and messages the SW: background-image, <video>
// frame (or a screenshot-crop request when the canvas is tainted), or an overlay-buried
// image. Real <img>/<svg><image> return null (native context covers). Self-contained
// (no imports); the guard stops double injection binding two listeners.
(() => {
  if (window.__stencilCtxProbe) return;
  window.__stencilCtxProbe = true;

  // mirror of lib/messages.js (classic content script — can't import)
  const MSG = { WAKE: 'stencil-wake', CTX: 'stencil-ctx' };

  // Wake the lazy SW on load so the menu exists before the first right-click —
  // receiving the message evaluates the worker, which builds the menu.
  try {
    chrome.runtime.sendMessage({ type: MSG.WAKE }, () => void chrome.runtime.lastError);
  } catch {
    /* ignore */
  }

  // ── Poster snapshot ──────────────────────────────────────────────────────
  // Some players strip <video poster> once playback starts, so a lazy read finds it
  // gone. Stamp every video's poster early onto the element (non-empty wins, so a later
  // empty never clobbers it) where both this probe and the popup scan — same isolated
  // world — can recover it.
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
  // The persisted poster for a video: the live attribute, else the early snapshot.
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
      if (u && !u.toLowerCase().startsWith('data:image/svg')) return u;
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

  // True when the video sits on its POSTER, not a real frame: never played (paused at
  // time 0) or no decoded data yet. drawImage() then yields frame 0 (commonly black),
  // so the caller should use the poster instead.
  const showingPoster = (v) => (v.paused && !v.currentTime) || v.readyState < 2;

  // Current frame of a <video> as a JPEG data URL, or null when the poster is
  // showing (use it instead) or the canvas is tainted (cross-origin) — the caller
  // then falls back to the poster, then a tab screenshot.
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

  // The <video> the right-click points at. Players lay a controls/link overlay over
  // the <video>, so closest() misses it; fall back to a geometric hit-test.
  const videoAt = (start, x, y) => {
    const direct = start.closest && start.closest('video');
    if (direct) return direct;
    if (x != null) {
      // Smallest <video> whose box contains the cursor — spatially correct, and (unlike
      // querySelector on an ancestor) never picks a different video when several share a
      // wrapper.
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

  // Absolute URL of the first <img>/<svg><image> or background-image element under
  // the cursor point — for photos buried beneath click-catcher overlays, where
  // neither the native image context nor an ancestor walk sees the picture.
  const imageUnderPoint = (x, y) => {
    if (x == null || typeof document.elementsFromPoint !== 'function') return null;
    for (const el of document.elementsFromPoint(x, y)) {
      // <img>: currentSrc/src are URL strings. <svg><image>: el.src is an
      // SVGAnimatedString (not a URL) — read the href attribute instead.
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

  // A link (<a href>) pointing straight at an image file (absolute URL). Lets the
  // menu act on the linked image when there's no <img>/background under the cursor —
  // restricted to image extensions so a normal page link never reveals the menu.
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

  // Background-image URL on `start` or its ancestors (absolute).
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

  // Element's rect in TOP-window coordinates — add each enclosing same-origin iframe's
  // offset so a tab screenshot crops correctly when the element is framed. Cross-origin
  // ancestors throw and stop the walk (best effort).
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

  // What can Stencil grab from the element under the cursor? Returns the data the
  // SW should remember, or null.
  const resolveTarget = (start, x, y) => {
    if (!start) return null;
    // A real <img>/<svg><image>: the native 'image' context already builds the menu, so
    // report ONLY the image URL (as `imgUrl`, never `url`) — enough for the SW to label the
    // Pin item (Pin ↔ Unpin), without revealing the background menu group (keyed on `url`).
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
      // The poster is a page-level preview image (often unlike any frame). Use the
      // persisted value (posterOf) so a player stripping the attribute after playback
      // doesn't hide it. Passed along for the menu's Preview submenu.
      let poster = '';
      const rawPoster = posterOf(video);
      if (rawPoster) {
        try {
          poster = new URL(rawPoster, location.href).href;
        } catch {
          poster = rawPoster;
        }
      }
      const frame = captureVideoFrame(video);
      // The video's media URL (http(s) only) — the openable source a pin keys on, so the
      // SW can label the Pin item (Pin ↔ Unpin) for this video.
      const rawMedia = video.currentSrc || video.src || '';
      let videoUrl = '';
      if (rawMedia.startsWith('http:') || rawMedia.startsWith('https:')) {
        try { videoUrl = new URL(rawMedia, location.href).href; } catch { videoUrl = rawMedia; }
      }
      // Tag it `video` so the click handler prefers this frame over info.srcUrl (the
      // media file, not a still). `posterShown` tells the click handler the video is on
      // its poster (not played) → use the poster, not a screenshot, when no frame read.
      if (frame) return { url: frame, video: true, poster, videoUrl };
      return { video: true, rect: topRect(video), dpr: window.devicePixelRatio || 1, poster, posterShown: showingPoster(video), videoUrl };
    }
    const bg = bgUrlFor(start);
    if (bg) return { url: bg };
    // A real image hidden under overlay elements at the cursor point.
    const under = imageUnderPoint(x, y);
    if (under) return { url: under };
    // Last resort: a link pointing straight at an image file.
    const link = imageLinkUrl(start);
    return link ? { url: link } : null;
  };

  document.addEventListener('contextmenu', (e) => {
    let data = null;
    try {
      data = resolveTarget(e.target, e.clientX, e.clientY);
    } catch {
      data = null;
    }
    // Send the cursor point too: the SW recaptures a video frame in-page at click time
    // (robust against worker restart / stale frame) and uses it to pick the right video.
    // The SW may be asleep / page navigating — a failed send is fine.
    const point = { x: e.clientX, y: e.clientY };
    try {
      chrome.runtime.sendMessage({ type: MSG.CTX, data, point });
    } catch {
      /* ignore */
    }
  }, true);
})();
