// MAIN-world scripts cannot import, so content/pageApiMain.js carries an inline mirror of
// these; the exported copies here are the tested source of truth — keep in sync.

// Inline-SVG data URIs count: lib/rasterize.js turns them into PNG.
export const bgImageUrl = (cssValue) => {
  const m = /url\((['"]?)(.*?)\1\)/i.exec(String(cssValue || ''));
  const url = m ? m[2].trim() : '';
  return url;
};

// Every url() in a CSS value, in order, minus bare `#fragment` refs — those point at an
// in-document paint server, filter or clip path, not an image.
export const cssImageUrls = (cssValue) => {
  const s = String(cssValue || '');
  if (!s.includes('url(')) return [];
  const re = /url\((['"]?)(.*?)\1\)/g;
  const out = [];
  let m;
  while ((m = re.exec(s))) {
    const u = (m[2] || '').trim();
    if (!u || u.startsWith('#')) continue;
    out.push(u);
  }
  return out;
};

// The URL token of each srcset candidate. A data: URL containing a comma splits wrongly —
// rare in srcset, not worth a state machine.
export const srcsetUrls = (srcset) => {
  const s = String(srcset || '').trim();
  if (!s) return [];
  const out = [];
  for (const cand of s.split(',')) {
    const url = cand.trim().split(/\s+/)[0];
    if (url) out.push(url);
  }
  return out;
};

// Icon `src`s are manifest-relative.
export const manifestIconUrls = (manifest, manifestUrl) => {
  const icons = manifest && Array.isArray(manifest.icons) ? manifest.icons : [];
  const out = [];
  for (const ic of icons) {
    if (!ic || !ic.src) continue;
    try { out.push(new URL(ic.src, manifestUrl).href); } catch { /* unresolvable src */ }
  }
  return out;
};

// Mirrors lib/stencil.js filenameFromUrl, dependency-free for the MAIN world.
export const nameFromUrl = (url, fallback = 'image') => {
  const s = String(url || '');
  try {
    if (s.startsWith('data:')) {
      const mime = /^data:([^;,]+)/.exec(s);
      const ext = mime ? (mime[1].split('/')[1] || 'png').replace('+xml', '') : 'png';
      return `${fallback}.${ext}`;
    }
    const u = new URL(s);
    const base = decodeURIComponent(u.pathname.split('/').filter(Boolean).pop() || '');
    if (base && /\.[a-z0-9]{2,4}$/i.test(base)) return base;
    return `${base || fallback}.png`;
  } catch {
    return `${fallback}.png`;
  }
};

// Decoded, sized, and not paused on frame 0 (the poster). Mirrors scan.js.
export const videoHasFrame = (v) =>
  !!(v && v.videoWidth && v.videoHeight && v.readyState >= 2 && !(v.paused && !v.currentTime));
