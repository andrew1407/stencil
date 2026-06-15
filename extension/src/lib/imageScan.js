// ── Page image scanner ──────────────────────────────────────────────────────
// scanPageForImages runs in the PAGE context (injected via
// chrome.scripting.executeScript), so it must be entirely self-contained — no
// imports, no references to module scope. It collects <img>, inline <svg><image>
// CSS background-image URLs (including ::before / ::after pseudo-elements), and
// <video> current frames, resolved to absolute, deduped, capped.
export const scanPageForImages = (limit) => {
  const out = [];
  const seen = new Set();
  const abs = (raw) => {
    if (!raw) return '';
    try { return new URL(raw, location.href).href; } catch { return ''; }
  };
  // `extra` carries video-only fields (videoUrl, hasFrame). A video with no
  // readable frame and no poster still lists (keyed on its media URL) so it can be
  // opened in a tab.
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

  // <video>: list as a video carrying its current frame (the still to edit/crop)
  // AND its media URL (to open/download). Cross-origin videos taint the canvas, so
  // the frame may be null — then the still falls back to the poster, and the popup
  // can still open the video in a tab.
  document.querySelectorAll('video').forEach(v => {
    const w = v.videoWidth, h = v.videoHeight;
    let frame = null;
    // readyState >= HAVE_CURRENT_DATA — otherwise drawImage paints a blank frame.
    if (w && h && v.readyState >= 2) {
      try {
        // Cap the longest side: the frame rides in the editor launch URL as a data
        // URL, and an un-capped 4K frame overflows Chrome's URL limit (about:blank).
        const s = Math.min(1, 1920 / Math.max(w, h));
        const cw = Math.max(1, Math.round(w * s)), ch = Math.max(1, Math.round(h * s));
        const c = document.createElement('canvas');
        c.width = cw; c.height = ch;
        c.getContext('2d').drawImage(v, 0, 0, cw, ch);
        frame = c.toDataURL('image/jpeg', 0.92);
      } catch { frame = null; }
    }
    // Only an http(s) media URL is openable from the extension; a page-created
    // blob: URL isn't reachable from another context, so leave it out.
    const raw = v.currentSrc || v.src || '';
    const videoUrl = /^https?:/i.test(raw) ? abs(raw) : '';
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
        if (u && !/^data:image\/svg/i.test(u)) push(u, 'bg', 0, 0, '');
      }
    }
  }
  return out;
};
