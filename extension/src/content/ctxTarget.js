// ── Right-click probe (content script) ───────────────────────────────────────
// Declared on <all_urls> and also injected into already-open tabs by the service
// worker on install/startup. On every contextmenu it works out what Stencil can
// grab from the element under the cursor and tells the service worker, which
// remembers it for the click handler:
//   • a CSS background-image (the element or an ancestor, incl. ::before/::after)
//     → { url }
//   • a <video> → its current frame as a data URL ({ url }); if the video is
//     cross-origin the canvas is tainted, so we instead send a screenshot-crop
//     request ({ video:true, rect, dpr }) the SW fulfils via captureVisibleTab.
//   • real <img>/<svg><image> → null (the native 'image' context + info.srcUrl
//     already cover those).
// Self-contained: declared content scripts can't use imports. The guard keeps a
// double injection (manifest + executeScript) from binding two listeners.
(() => {
  if (window.__stencilCtxProbe) return;
  window.__stencilCtxProbe = true;

  // Wake the (lazy) service worker on load so it creates the context menu before
  // the first right-click — otherwise the menu wouldn't exist yet and only the
  // native menu would show. The message itself needs no handling; receiving it
  // is enough to evaluate the worker (which builds the menu at top level).
  try { chrome.runtime.sendMessage({ type: 'stencil-wake' }, () => void chrome.runtime.lastError); } catch { /* ignore */ }

  const firstCssImageUrl = (bg) => {
    if (!bg || bg === 'none') return null;
    const re = /url\((['"]?)(.*?)\1\)/g;
    let m;
    while ((m = re.exec(bg))) {
      const u = m[2];
      if (u && !/^data:image\/svg/i.test(u)) return u;
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

  const isImageEl = (el) => !!(el && el.closest && (el.closest('img') || el.closest('image')));

  // Cap the captured frame's longest side. The frame rides in the editor launch
  // URL (#stencil=…) as a data URL; an un-capped 4K frame blows past Chrome's URL
  // limit and the editor tab lands on about:blank. 1920 is ample for a crop.
  const FRAME_MAX_SIDE = 1920;

  // Current frame of a <video> as a JPEG data URL, or null if it can't be read.
  // Returns null when no frame is decoded yet (readyState < HAVE_CURRENT_DATA —
  // drawing then yields a blank frame) or when the canvas is tainted (cross-origin
  // media), in which case the caller falls back to a tab screenshot.
  const captureVideoFrame = (video) => {
    if (!video) return null;
    const vw = video.videoWidth, vh = video.videoHeight;
    if (!vw || !vh || video.readyState < 2) return null;
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

  // The <video> the right-click is really pointing at. Players commonly lay a
  // transparent controls/poster overlay OVER the <video> as a sibling, so the
  // event target is neither the video nor an ancestor of it — closest() misses.
  // Three escalating attempts:
  //   1. closest() — the simple, common case.
  //   2. hit-test the cursor point — sees through an overlay to the video stacked
  //      under it (and picks the right one when there are several videos).
  //   3. geometric test — a <video> whose box contains the cursor, used when the
  //      hit-test reports only a wrapper/shadow host (closed shadow DOM, custom
  //      players) and never the video itself.
  const videoAt = (start, x, y) => {
    const direct = start.closest && start.closest('video');
    if (direct) return direct;
    if (typeof document.elementsFromPoint === 'function' && x != null) {
      for (const el of document.elementsFromPoint(x, y)) {
        if (el.tagName === 'VIDEO') return el;
        const v = el.querySelector && el.querySelector('video');
        if (v) return v;
      }
    }
    if (x != null) {
      for (const v of document.querySelectorAll('video')) {
        const r = v.getBoundingClientRect();
        if (r.width && r.height && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return v;
      }
    }
    return null;
  };

  // Background-image URL on `start` or its ancestors (absolute).
  const bgUrlFor = (start) => {
    for (let node = start; node && node.nodeType === 1; node = node.parentElement) {
      const u = cssImageUrlOf(node);
      if (u) {
        try { return new URL(u, location.href).href; } catch { return null; }
      }
    }
    return null;
  };

  // An element's rect in the TOP window's coordinates — add each enclosing
  // (same-origin) iframe's offset, so a tab screenshot can be cropped correctly
  // even when the element is inside a frame. Cross-origin ancestors throw and
  // stop the walk (best effort).
  const topRect = (el) => {
    const r = el.getBoundingClientRect();
    let x = r.x, y = r.y, win = el.ownerDocument.defaultView;
    try {
      while (win && win.frameElement) {
        const fr = win.frameElement.getBoundingClientRect();
        x += fr.x; y += fr.y;
        win = win.parent;
      }
    } catch { /* cross-origin ancestor — use what we have */ }
    return { x, y, width: r.width, height: r.height };
  };

  // What can Stencil grab from the element under the cursor? Returns the data the
  // SW should remember, or null.
  const resolveTarget = (start, x, y) => {
    if (!start || isImageEl(start)) return null;
    const video = videoAt(start, x, y);
    if (video) {
      const frame = captureVideoFrame(video);
      // Tag it `video` so the click handler always prefers this frame over Chrome's
      // info.srcUrl (which for a <video> is the media file, not a still).
      if (frame) return { url: frame, video: true };
      return { video: true, rect: topRect(video), dpr: window.devicePixelRatio || 1 };
    }
    const bg = bgUrlFor(start);
    return bg ? { url: bg } : null;
  };

  document.addEventListener('contextmenu', (e) => {
    let data = null;
    try { data = resolveTarget(e.target, e.clientX, e.clientY); } catch { data = null; }
    // Send the cursor point too: for a video the SW recaptures the frame in-page at
    // click time (robust against an MV3 worker restart / a stale recorded frame),
    // and the point lets it pick the right video.
    const point = { x: e.clientX, y: e.clientY };
    // The SW may be asleep / the page navigating — failing to reach it is fine.
    try { chrome.runtime.sendMessage({ type: 'stencil-ctx', data, point }); } catch { /* ignore */ }
  }, true);
})();
