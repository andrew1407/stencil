// ── Right-click probe (content script) ───────────────────────────────────────
// Declared on <all_urls> and also injected into already-open tabs by the SW on
// install/startup. On every contextmenu it resolves what Stencil can grab under
// the cursor and tells the SW (which the click handler then reads):
//   • CSS background-image (element or ancestor, incl. ::before/::after) → { url }
//   • <video> → current frame as a data URL { url }; if cross-origin (tainted
//     canvas) → a screenshot-crop request { video:true, rect, dpr } instead.
//   • real <img>/<svg><image> → null (native 'image' context + info.srcUrl cover it).
// Self-contained (content scripts can't import). The guard stops a double
// injection (manifest + executeScript) from binding two listeners.
(() => {
  if (window.__stencilCtxProbe) return;
  window.__stencilCtxProbe = true;

  // Wake the lazy SW on load so the context menu exists before the first
  // right-click. The message needs no handling — receiving it evaluates the
  // worker, which builds the menu at top level.
  try { chrome.runtime.sendMessage({ type: 'stencil-wake' }, () => void chrome.runtime.lastError); } catch { /* ignore */ }

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

  const isImageEl = (el) => !!(el && el.closest && (el.closest('img') || el.closest('image')));

  // Cap the captured frame's longest side: it rides in the editor launch URL as a
  // data URL, and an un-capped 4K frame overflows Chrome's URL limit (about:blank).
  const FRAME_MAX_SIDE = 1920;

  // Current frame of a <video> as a JPEG data URL, or null when no frame is decoded
  // yet (readyState < HAVE_CURRENT_DATA → blank) or the canvas is tainted
  // (cross-origin) — the caller then falls back to a tab screenshot.
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

  // The <video> the right-click really points at. Players often lay a controls
  // overlay over the <video> as a sibling, so closest() misses it. Three escalating
  // attempts: 1) closest(); 2) hit-test the cursor point (sees through an overlay
  // to the video under it); 3) geometric test (a <video> box containing the cursor,
  // for when the hit-test only reports a wrapper / closed shadow host).
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

  // An element's rect in TOP-window coordinates — add each enclosing same-origin
  // iframe's offset so a tab screenshot crops correctly when the element is framed.
  // Cross-origin ancestors throw and stop the walk (best effort).
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
      // Tag it `video` so the click handler prefers this frame over info.srcUrl
      // (which for a <video> is the media file, not a still).
      if (frame) return { url: frame, video: true };
      return { video: true, rect: topRect(video), dpr: window.devicePixelRatio || 1 };
    }
    const bg = bgUrlFor(start);
    return bg ? { url: bg } : null;
  };

  document.addEventListener('contextmenu', (e) => {
    let data = null;
    try { data = resolveTarget(e.target, e.clientX, e.clientY); } catch { data = null; }
    // Send the cursor point too: the SW recaptures a video frame in-page at click
    // time (robust against a worker restart / stale frame) and uses it to pick the
    // right video. The SW may be asleep / page navigating — a failed send is fine.
    const point = { x: e.clientX, y: e.clientY };
    try { chrome.runtime.sendMessage({ type: 'stencil-ctx', data, point }); } catch { /* ignore */ }
  }, true);
})();
