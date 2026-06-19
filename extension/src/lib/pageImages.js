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

// True for an http(s) URL — the only kind the editor hand-off can re-fetch/track.
export const isHttpUrl = (url) => /^https?:\/\//i.test(String(url || ''));
