// ── Page image scanner ──────────────────────────────────────────────────────
// Runs in the PAGE context (injected via chrome.scripting), so it must be fully
// self-contained — no imports, no module-scope refs. Collects <img>, inline
// <svg><image>, CSS background-image URLs (incl. ::before/::after), and <video>
// current frames — resolved to absolute, deduped, capped.
export const scanPageForImages = (limit) => {
  const out = [];
  const seen = new Set();
  const abs = (raw) => {
    if (!raw) return '';
    try {
      return new URL(raw, location.href).href;
    } catch {
      return '';
    }
  };
  // `extra` carries video-only fields (videoUrl, hasFrame). A frameless, posterless
  // video still lists (keyed on its media URL) so it can be opened in a tab.
  const push = (raw, kind, w, h, alt, extra = {}) => {
    if (out.length >= limit) return;
    const src = abs(raw);
    const key = src || extra.videoUrl || '';
    if (!key || seen.has(key)) return;
    seen.add(key);
    out.push({ src, kind, w: w || 0, h: h || 0, alt: alt || '', ...extra });
  };

  document.querySelectorAll('img').forEach(img =>
    push(img.currentSrc || img.src, 'img', img.naturalWidth, img.naturalHeight, img.alt));

  document.querySelectorAll('svg image').forEach(im =>
    push(im.getAttribute('href') || im.getAttribute('xlink:href'), 'img', 0, 0, ''));

  // <video>: list with its current frame (the still to edit/crop) AND its media URL
  // (to open/download). A cross-origin video taints the canvas, so the frame may be
  // null — the still then falls back to the poster.
  document.querySelectorAll('video').forEach(v => {
    const w = v.videoWidth, h = v.videoHeight;
    let frame = null;
    // Capture a frame only when the video is actually showing one: it must have
    // decoded data AND have been played. A video paused at time 0 displays its
    // POSTER, while drawImage() would grab frame 0 (commonly black) — so skip it
    // and let the poster stand in (below).
    if (w && h && v.readyState >= 2 && !(v.paused && !v.currentTime)) {
      try {
        // Cap the longest side so the frame's data URL doesn't overflow the editor
        // launch URL (a 4K frame would land the editor tab on about:blank).
        const s = Math.min(1, 1920 / Math.max(w, h));
        const cw = Math.max(1, Math.round(w * s)), ch = Math.max(1, Math.round(h * s));
        const c = document.createElement('canvas');
        c.width = cw; c.height = ch;
        c.getContext('2d').drawImage(v, 0, 0, cw, ch);
        frame = c.toDataURL('image/jpeg', 0.92);
      } catch {
        frame = null;
      }
    }
    // Only an http(s) media URL is reachable from another context; a page-created
    // blob: URL isn't, so leave it out.
    const raw = v.currentSrc || v.src || '';
    const videoUrl = (raw.startsWith('http:') || raw.startsWith('https:')) ? abs(raw) : '';
    // The poster is a page-level preview image, often unrelated to any frame. List
    // it as its OWN image item (so it can be opened/cropped independently), and tag
    // the video item with it. Fall back to the probe's persisted snapshot
    // (`__stencilPoster`, same extension isolated world) since some players strip
    // the live poster attribute once playback starts.
    const rawPoster = v.poster || v.__stencilPoster || '';
    const poster = rawPoster ? abs(rawPoster) : '';
    if (poster) {
      // The same image is often ALSO a plain <img> on the page (scanned first) — tag
      // that existing row as a poster rather than dropping a duplicate; otherwise add
      // the poster as its own image item.
      const existing = out.find(it => it.src === poster);
      if (existing) existing.poster = true;
      else push(poster, 'img', 0, 0, v.getAttribute('aria-label') || 'video poster', { poster: true });
    }
    // The video item now represents the FRAME (its still); poster is its own item
    // above, so it isn't reused as the video src/key here (that would collide and
    // drop one of the two). A frameless video still lists via its media URL.
    push(frame || '', 'video', w, h, v.getAttribute('aria-label') || 'video', { videoUrl, hasFrame: !!frame, posterUrl: poster });
  });

  for (const el of document.querySelectorAll('*')) {
    if (out.length >= limit) break;
    for (const pseudo of [null, '::before', '::after']) {
      const bg = getComputedStyle(el, pseudo).backgroundImage;
      if (!bg || bg === 'none') continue;
      const re = /url\((['"]?)(.*?)\1\)/g;
      let m;
      while ((m = re.exec(bg))) {
        const u = m[2];
        if (u && !u.toLowerCase().startsWith('data:image/svg')) push(u, 'bg', 0, 0, '');
      }
    }
  }
  return out;
};
