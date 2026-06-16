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
    try { return new URL(raw, location.href).href; } catch { return ''; }
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
    // readyState >= HAVE_CURRENT_DATA — otherwise drawImage paints a blank frame.
    if (w && h && v.readyState >= 2) {
      try {
        // Cap the longest side so the frame's data URL doesn't overflow the editor
        // launch URL (a 4K frame would land the editor tab on about:blank).
        const s = Math.min(1, 1920 / Math.max(w, h));
        const cw = Math.max(1, Math.round(w * s)), ch = Math.max(1, Math.round(h * s));
        const c = document.createElement('canvas');
        c.width = cw; c.height = ch;
        c.getContext('2d').drawImage(v, 0, 0, cw, ch);
        frame = c.toDataURL('image/jpeg', 0.92);
      } catch { frame = null; }
    }
    // Only an http(s) media URL is reachable from another context; a page-created
    // blob: URL isn't, so leave it out.
    const raw = v.currentSrc || v.src || '';
    const videoUrl = (raw.startsWith('http:') || raw.startsWith('https:')) ? abs(raw) : '';
    const still = frame || v.poster || '';
    push(still, 'video', w, h, v.getAttribute('aria-label') || 'video', { videoUrl, hasFrame: !!frame });
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
