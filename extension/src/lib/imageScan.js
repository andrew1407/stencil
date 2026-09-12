// scanPageForImages is injected via chrome.scripting, so it is fully self-contained: the
// helpers inside it are inline copies of lib/pageImages.js — keep them in sync.
// MAX_IMAGES caps what one page yields; BLOCKED_SCHEMES can never be injected into.
export const MAX_IMAGES = 1000;
export const BLOCKED_SCHEMES = Object.freeze(['chrome:', 'edge:', 'about:', 'chrome-extension:', 'view-source:']);

// All-frames results → one list, deduped by src (first frame wins), capped at `limit`.
export const mergeScanFrames = (results, limit = MAX_IMAGES) => {
  const out = [];
  const seen = new Set();
  for (const r of results || []) {
    for (const it of (r?.result || [])) {
      if (out.length >= limit) break;
      if (!seen.has(it.src)) { seen.add(it.src); out.push(it); }
    }
  }
  return out;
};

export const scanPageForImages = async (limit) => {
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
  // Inline mirror of lib/pageImages.js cssImageUrls: every url() minus bare #fragment refs.
  const cssImageUrls = (cssValue) => {
    const s = String(cssValue || '');
    if (!s.includes('url(')) return [];   // cheap skip for none/normal/auto/gradients
    const re = /url\((['"]?)(.*?)\1\)/g;
    const urls = [];
    let m;
    while ((m = re.exec(s))) {
      const u = (m[2] || '').trim();
      if (!u || u.startsWith('#')) continue;
      urls.push(u);
    }
    return urls;
  };
  // Inline mirror of lib/pageImages.js srcsetUrls.
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
  // One getComputedStyle per (element, pseudo); mask/-webkit-mask are engine-dependent.
  const CSS_IMG_PROPS = ['backgroundImage', 'content', 'borderImageSource', 'listStyleImage', 'maskImage', 'webkitMaskImage', 'cursor', 'shapeOutside'];
  const PSEUDOS = [null, '::before', '::after'];
  // A prefetch <link> has no `as`, so its href must look like an image.
  const IMG_EXT = /\.(png|jpe?g|gif|webp|avif|bmp|ico|cur|svg|tiff?)(?:[?#]|$)/i;

  // A frameless, posterless video still lists, keyed on its media URL.
  const push = (raw, kind, w, h, alt, extra = {}) => {
    if (out.length >= limit) return;
    const src = abs(raw);
    const key = src || extra.videoUrl || '';
    if (!key || seen.has(key)) return;
    seen.add(key);
    out.push({ src, kind, w: w || 0, h: h || 0, alt: alt || '', ...extra });
  };

  // Started before the DOM walk so the fetch overlaps it; awaited at the end.
  const manifestIcons = (async () => {
    const link = document.querySelector('link[rel~="manifest"]');
    if (!link) return [];
    try {
      const manifestUrl = abs(link.getAttribute('href'));
      // The href is page-supplied and the fetch carries the page's cookies, so it may only
      // reach the page's OWN origin. This function is injected and cannot import
      // lib/urlGuard.js; same-origin is strictly tighter than its same-host carve-out.
      if (new URL(manifestUrl).origin !== location.origin) return [];
      const manifest = await (await fetch(manifestUrl, { credentials: 'include' })).json();
      return (Array.isArray(manifest.icons) ? manifest.icons : [])
        .filter((ic) => ic && ic.src)
        .map((ic) => ({ src: abs(new URL(ic.src, manifestUrl).href), purpose: ic.purpose || 'app icon' }));
    } catch { return []; /* no manifest / blocked / bad JSON */ }
  })();

  document.querySelectorAll('img').forEach(img =>
    push(img.currentSrc || img.src, 'img', img.naturalWidth, img.naturalHeight, img.alt));

  // The responsive candidates the browser did not pick — often another format.
  document.querySelectorAll('img[srcset], source[srcset]').forEach(el =>
    srcsetUrls(el.getAttribute('srcset')).forEach(u => push(u, 'img', 0, 0, '')));

  document.querySelectorAll('image, feImage').forEach(im =>
    push(im.getAttribute('href') || im.getAttribute('xlink:href'), 'img', 0, 0, ''));

  document.querySelectorAll('input[type="image"]').forEach(el =>
    push(el.currentSrc || el.getAttribute('src'), 'img', el.naturalWidth || 0, el.naturalHeight || 0, el.alt || ''));

  // meta:true = page furniture, gated by the "Icons & metadata" toggle, not "Images".
  document.querySelectorAll('link[rel~="icon"], link[rel="apple-touch-icon"], link[rel="apple-touch-icon-precomposed"], link[rel="mask-icon"], link[rel="preload"][as="image"], link[rel="prefetch"]').forEach(link => {
    const rel = (link.getAttribute('rel') || '').toLowerCase();
    const href = link.getAttribute('href') || '';
    if (href && (!rel.includes('prefetch') || IMG_EXT.test(href))) push(href, 'img', 0, 0, 'icon', { meta: true });
    srcsetUrls(link.getAttribute('imagesrcset')).forEach(u => push(u, 'img', 0, 0, 'icon', { meta: true }));
  });

  document.querySelectorAll('meta[property="og:image"], meta[property="og:image:url"], meta[property="og:image:secure_url"], meta[name="twitter:image"], meta[name="twitter:image:src"], meta[itemprop="image"]').forEach(meta =>
    push(meta.getAttribute('content'), 'img', 0, 0, 'preview', { meta: true }));

  // A video lists with its current frame (the still) and its media URL; a cross-origin
  // video taints the canvas, so the frame may be null and the poster stands in.
  document.querySelectorAll('video').forEach(v => {
    const w = v.videoWidth, h = v.videoHeight;
    let frame = null;
    // Paused at time 0 the element shows its poster while drawImage grabs frame 0
    // (commonly black) — skip that case.
    if (w && h && v.readyState >= 2 && !(v.paused && !v.currentTime)) {
      try {
        // Capped so the frame's data URL cannot overflow the editor launch URL.
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
    // A page-created blob: URL is unreachable from another context.
    const raw = v.currentSrc || v.src || '';
    const videoUrl = (raw.startsWith('http:') || raw.startsWith('https:')) ? abs(raw) : '';
    // `__stencilPoster` is the probe's snapshot: some players strip the attribute on playback.
    const rawPoster = v.poster || v.__stencilPoster || '';
    const poster = rawPoster ? abs(rawPoster) : '';
    if (poster) {
      // The poster is often also a plain <img> scanned earlier — tag that row, don't duplicate.
      const existing = out.find(it => it.src === poster);
      if (existing) existing.poster = true;
      else push(poster, 'img', 0, 0, v.getAttribute('aria-label') || 'video poster', { poster: true });
    }
    // The poster must not double as the video's src/key: the two would collide and one drop.
    push(frame || '', 'video', w, h, v.getAttribute('aria-label') || 'video', { videoUrl, hasFrame: !!frame, posterUrl: poster });
  });

  for (const el of document.querySelectorAll('*')) {
    if (out.length >= limit) break;
    for (const pseudo of PSEUDOS) {
      let cs;
      try { cs = getComputedStyle(el, pseudo); } catch { continue; }
      for (const prop of CSS_IMG_PROPS) {
        for (const u of cssImageUrls(cs[prop])) push(u, 'bg', 0, 0, '');
      }
    }
  }

  for (const ic of await manifestIcons) {
    if (out.length >= limit) break;
    push(ic.src, 'img', 0, 0, ic.purpose, { meta: true });
  }

  return out;
};
