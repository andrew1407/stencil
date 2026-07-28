// ── Pure helpers for the page-global window.stencil API ─────────────────────
// The DOM scan runs in the page's MAIN world (so entries hold live element refs the
// user's console can touch), and MAIN-world scripts can't import — so content/pageApiMain.js
// carries an inline MIRROR of these. These exported copies are the tested source of
// truth; keep in sync. No DOM access here beyond what's passed in.

// Extract the URL from a CSS background-image value, or '' when there is none / it's
// an inline SVG data URL (not a real shareable image). Handles url("…")/url('…')/url(…).
export const bgImageUrl = (cssValue) => {
  const m = /url\((['"]?)(.*?)\1\)/i.exec(String(cssValue || ''));
  const url = m ? m[2].trim() : '';
  if (!url || url.startsWith('data:image/svg')) return '';
  return url;
};

// EVERY image URL referenced by a CSS value — background-image, content (::before/::after),
// border-image-source, list-style-image, mask-image/-webkit-mask-image, cursor, shape-outside,
// and image-set() (which nests url() tokens). Returns them in order, dropping inline-SVG data
// URIs (not shareable) and bare `#fragment` refs — url(#mask)/url(#clip)/url(#filter) point at
// an in-document SVG paint server, filter, or clip path, NOT an image. Generalises bgImageUrl.
export const cssImageUrls = (cssValue) => {
  const s = String(cssValue || '');
  // Fast-path the overwhelming majority of computed values (none / normal / auto / gradients)
  // with a cheap substring test, so the regex is only allocated + run when a url() is present.
  if (!s.includes('url(')) return [];
  const re = /url\((['"]?)(.*?)\1\)/g;
  const out = [];
  let m;
  while ((m = re.exec(s))) {
    const u = (m[2] || '').trim();
    if (!u || u.startsWith('#') || u.startsWith('data:image/svg')) continue;
    out.push(u);
  }
  return out;
};

// URLs listed in a srcset / imagesrcset attribute. Each candidate is "URL [descriptor]"
// (e.g. `small.jpg 480w, large.jpg 1024w` or `img.png 1x, img@2x.png 2x`), comma-separated;
// the descriptor is dropped and the leading token kept. (A data: URL containing a comma is
// split incorrectly — rare in srcset and not worth a full state-machine parser here.)
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

// Absolute icon URLs from a parsed web-app-manifest object, resolved against the manifest's
// own URL (icon `src`s are manifest-relative). Non-object / iconless manifests yield [].
export const manifestIconUrls = (manifest, manifestUrl) => {
  const icons = manifest && Array.isArray(manifest.icons) ? manifest.icons : [];
  const out = [];
  for (const ic of icons) {
    if (!ic || !ic.src) continue;
    try { out.push(new URL(ic.src, manifestUrl).href); } catch { /* unresolvable src */ }
  }
  return out;
};

// A reasonable file name for an image URL (mirrors lib/stencil.js filenameFromUrl,
// kept dependency-free for the MAIN world). Falls back to `<fallback>.png`.
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

// True when a <video> is currently showing a real, capturable frame (decoded data,
// real dimensions, and not paused on frame 0 i.e. the poster). Mirrors imageScan.js.
export const videoHasFrame = (v) =>
  !!(v && v.videoWidth && v.videoHeight && v.readyState >= 2 && !(v.paused && !v.currentTime));
