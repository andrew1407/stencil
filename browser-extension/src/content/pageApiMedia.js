// window.stencil, part 1 of 4 (pageApiMedia → pageApiScan → pageApiHighlight → pageApiMain, one
// MAIN-world registration): the bridge channels and the pure media helpers. Classic scripts share
// one namespace object on window until pageApiMain deletes it; the helpers mirror
// lib/image/pageImages.js (pageApiMainMirror.test.js pins them).
(() => {
  // Never clobber the editor's OWN window.stencil (no __stencil tag): the editor page wins.
  if (window.stencil) {
    if (window.stencil.__stencil === 'page') return;
    if (!window.stencil.__stencil) return;
  }
  const NS = '__stencilPageApiParts';
  if (Object.prototype.hasOwnProperty.call(window, NS)) return;
  const K = {};
  try { Object.defineProperty(window, NS, { value: K, configurable: true, enumerable: false, writable: false }); } catch { return; }

  // mirror of lib/messages.js (MAIN-world script — can't import)
  const MSG = { PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop', PAGE_PIN: 'stencil-page-pin', PAGE_REQUEST_SYNC: 'stencil-page-request-sync', PAGE_DISABLE: 'stencil-page-disable', PAGE_SET_FILTERS: 'stencil-page-set-filters' };
  const SRC = { PAGE_API: 'stencil-page-api', PAGE_FILTERS: 'stencil-page-filters', PAGE_PINS: 'stencil-page-pins', PAGE_EDITED: 'stencil-page-edited', PAGE_HL_COLOR: 'stencil-page-hl-color' };

  // Pinned (this site) / opened-in-an-editor source URLs, pushed live by the bridge; a pin
  // write updates pinnedSources optimistically so the getter flips at once.
  const pinnedSources = new Set();
  const editedSources = new Set();

  const send = (message) => window.postMessage({ source: SRC.PAGE_API, message }, '*');

  const bgImageUrl = (cssValue) => {
    const m = /url\((['"]?)(.*?)\1\)/i.exec(String(cssValue || ''));
    const url = m ? m[2].trim() : '';
    return url || '';
  };
  // Inline mirror of lib/image/pageImages.js cssImageUrls (pageApiMainMirror.test.js).
  const cssImageUrls = (cssValue) => {
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
  // Inline mirror of lib/image/pageImages.js srcsetUrls.
  const srcsetUrls = (srcset) => {
    const s = String(srcset || '').trim();
    if (!s) return [];
    const urls = [];
    for (const cand of s.split(',')) {
      const u = cand.trim().split(/\s+/)[0];
      if (u) urls.push(u);
    }
    return urls;
  };
  const nameFromUrl = (url, fallback = 'image') => {
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
  const videoHasFrame = (v) => !!(v && v.videoWidth && v.videoHeight && v.readyState >= 2 && !(v.paused && !v.currentTime));

  // Lowercase media format from a URL / data: URI ('' if unknown) — mirrors lib/highlight/filters.js.
  const normFmt = (ext) => ext.toLowerCase().replace('jpeg', 'jpg').replace('svg+xml', 'svg').replace('quicktime', 'mov');
  const formatOf = (src) => {
    if (!src) return '';
    if (src.startsWith('data:')) { const m = /^data:(?:image|video)\/([a-z0-9.+-]+)/i.exec(src); return m ? normFmt(m[1]) : ''; }
    let path = src;
    try { path = new URL(src, 'http://_/').pathname; } catch { /* keep raw */ }
    const m = /\.([a-z0-9]{2,5})(?:[?#]|$)/i.exec(path);
    return m ? normFmt(m[1]) : '';
  };

  // Draw a <video>'s current frame to a JPEG data URL (null if not ready or tainted).
  const captureVideoFrame = (v) => {
    if (!videoHasFrame(v)) return null;
    try {
      const k = Math.min(1, 1920 / Math.max(v.videoWidth, v.videoHeight));
      const c = document.createElement('canvas');
      c.width = Math.max(1, Math.round(v.videoWidth * k));
      c.height = Math.max(1, Math.round(v.videoHeight * k));
      c.getContext('2d').drawImage(v, 0, 0, c.width, c.height);
      return c.toDataURL('image/jpeg', 0.92);
    } catch { return null; }   // cross-origin / tainted
  };

  // CSS properties whose value can hold an image url() — mirrors scan.js.
  const CSS_IMG_PROPS = ['backgroundImage', 'content', 'borderImageSource', 'listStyleImage', 'maskImage', 'webkitMaskImage', 'cursor', 'shapeOutside'];
  const PSEUDOS = [null, '::before', '::after'];
  const firstCssImageUrl = (el, pseudo = null) => {
    let cs;
    try { cs = getComputedStyle(el, pseudo); } catch { return ''; }   // cross-origin sheet
    for (const prop of CSS_IMG_PROPS) { const u = cssImageUrls(cs[prop])[0]; if (u) return u; }
    return '';
  };
  const elementUrl = (el) => {
    if (!el || el.nodeType !== 1) return { kind: null, url: '' };
    const tag = (el.tagName || '').toLowerCase();
    if (tag === 'img') return { kind: 'image', url: el.currentSrc || el.getAttribute('src') || '' };
    if (tag === 'image' || tag === 'feimage') return { kind: 'image', url: el.getAttribute('href') || el.getAttribute('xlink:href') || '' };
    if (tag === 'input' && (el.getAttribute('type') || '').toLowerCase() === 'image') return { kind: 'image', url: el.currentSrc || el.getAttribute('src') || '' };
    if (tag === 'video') return { kind: 'video', url: el.currentSrc || el.getAttribute('src') || el.getAttribute('poster') || '' };
    if (tag === 'link' && /(^|\s)(icon|apple-touch-icon(-precomposed)?|mask-icon)(\s|$)/i.test(el.getAttribute('rel') || '')) return { kind: 'image', url: el.getAttribute('href') || '' };
    if (tag === 'meta' && (el.getAttribute('property') || el.getAttribute('name') || el.getAttribute('itemprop') || '').toLowerCase().includes('image')) return { kind: 'image', url: el.getAttribute('content') || '' };
    const bg = firstCssImageUrl(el); if (bg) return { kind: 'background', url: bg };
    return { kind: null, url: '' };
  };

  // A CSS background has no intrinsic size without loading it: fall back to the rendered box.
  const entryDims = (el, kind) => {
    if (kind === 'video') return { w: el.videoWidth || 0, h: el.videoHeight || 0 };
    if (el && el.naturalWidth) return { w: el.naturalWidth, h: el.naturalHeight || 0 };
    return { w: (el && el.offsetWidth) || 0, h: (el && el.offsetHeight) || 0 };
  };

  Object.assign(K, {
    MSG, SRC, pinnedSources, editedSources, send, cssImageUrls, srcsetUrls, nameFromUrl, videoHasFrame,
    normFmt, formatOf, captureVideoFrame, CSS_IMG_PROPS, PSEUDOS, elementUrl, entryDims,
  });
})();
