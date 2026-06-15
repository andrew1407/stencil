// ── Page image scanner ──────────────────────────────────────────────────────
// scanPageForImages runs in the PAGE context (injected via
// chrome.scripting.executeScript), so it must be entirely self-contained — no
// imports, no references to module scope. It collects <img>, inline <svg><image>
// and CSS background-image URLs, resolved to absolute, deduped, capped.
export const scanPageForImages = (limit) => {
  const out = [];
  const seen = new Set();
  const push = (raw, kind, w, h, alt) => {
    if (!raw || out.length >= limit) return;
    let abs;
    try { abs = new URL(raw, location.href).href; } catch { return; }
    if (seen.has(abs)) return;
    seen.add(abs);
    out.push({ src: abs, kind, w: w || 0, h: h || 0, alt: alt || '' });
  };

  document.querySelectorAll('img').forEach(img =>
    push(img.currentSrc || img.src, 'img', img.naturalWidth, img.naturalHeight, img.alt));

  document.querySelectorAll('svg image').forEach(im =>
    push(im.getAttribute('href') || im.getAttribute('xlink:href'), 'img', 0, 0, ''));

  for (const el of document.querySelectorAll('*')) {
    if (out.length >= limit) break;
    const bg = getComputedStyle(el).backgroundImage;
    if (!bg || bg === 'none') continue;
    const re = /url\((['"]?)(.*?)\1\)/g;
    let m;
    while ((m = re.exec(bg))) {
      const u = m[2];
      if (u && !/^data:image\/svg/i.test(u)) push(u, 'bg', 0, 0, '');
    }
  }
  return out;
};
