// ── Page image scanner ──────────────────────────────────────────────────────
// Injected via chrome.scripting, so fully self-contained (helpers below are inline copies
// of lib/pageImages.js — keep in sync). Collects every image reference in the document
// (see the per-block comments): <img>/srcset/<picture>, <svg><image>/<feImage>, <input
// type=image>, <video> frames+posters, favicons/meta/preloads, manifest icons, and every
// CSS image. Absolute, deduped, capped. Async: the manifest is fetched (chrome.scripting awaits).
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
  // Inline mirror of lib/pageImages.js cssImageUrls — every image url() in a CSS value,
  // minus inline-SVG data URIs and bare #fragment refs (paint-server/filter/clip targets).
  const cssImageUrls = (cssValue) => {
    const s = String(cssValue || '');
    if (!s.includes('url(')) return [];   // cheap skip for none/normal/auto/gradients
    const re = /url\((['"]?)(.*?)\1\)/g;
    const urls = [];
    let m;
    while ((m = re.exec(s))) {
      const u = (m[2] || '').trim();
      if (!u || u.startsWith('#') || u.startsWith('data:image/svg')) continue;
      urls.push(u);
    }
    return urls;
  };
  // Inline mirror of lib/pageImages.js srcsetUrls — the URL token of each srcset candidate.
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
  // CSS properties whose value can carry an image url(). Read off ONE getComputedStyle per
  // (element, pseudo) so the walk stays cheap. mask/-webkit-mask both listed (engine-dependent).
  const CSS_IMG_PROPS = ['backgroundImage', 'content', 'borderImageSource', 'listStyleImage', 'maskImage', 'webkitMaskImage', 'cursor', 'shapeOutside'];
  const PSEUDOS = [null, '::before', '::after'];
  // A prefetch <link> has no `as`, so only treat it as an image when its href clearly is one.
  const IMG_EXT = /\.(png|jpe?g|gif|webp|avif|bmp|ico|cur|svg|tiff?)(?:[?#]|$)/i;

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

  // srcset alternates (<img srcset> + <picture><source srcset>): the responsive candidates
  // the browser DIDN'T pick — often a different format (webp/avif) worth grabbing on its own.
  document.querySelectorAll('img[srcset], source[srcset]').forEach(el =>
    srcsetUrls(el.getAttribute('srcset')).forEach(u => push(u, 'img', 0, 0, '')));

  document.querySelectorAll('image, feImage').forEach(im =>
    push(im.getAttribute('href') || im.getAttribute('xlink:href'), 'img', 0, 0, ''));

  // <input type=image> — an image used as a form submit button.
  document.querySelectorAll('input[type="image"]').forEach(el =>
    push(el.currentSrc || el.getAttribute('src'), 'img', el.naturalWidth || 0, el.naturalHeight || 0, el.alt || ''));

  // Favicons + apple-touch / mask icons, image preloads/prefetches (with their imagesrcset).
  // Flagged meta:true — page furniture, gated by the "Icons & metadata" toggle, not "Images".
  document.querySelectorAll('link[rel~="icon"], link[rel="apple-touch-icon"], link[rel="apple-touch-icon-precomposed"], link[rel="mask-icon"], link[rel="preload"][as="image"], link[rel="prefetch"]').forEach(link => {
    const rel = (link.getAttribute('rel') || '').toLowerCase();
    const href = link.getAttribute('href') || '';
    if (href && (!rel.includes('prefetch') || IMG_EXT.test(href))) push(href, 'img', 0, 0, 'icon', { meta: true });
    srcsetUrls(link.getAttribute('imagesrcset')).forEach(u => push(u, 'img', 0, 0, 'icon', { meta: true }));
  });

  // Social-sharing / structured-data preview images (Open Graph, Twitter, schema.org).
  document.querySelectorAll('meta[property="og:image"], meta[property="og:image:url"], meta[property="og:image:secure_url"], meta[name="twitter:image"], meta[name="twitter:image:src"], meta[itemprop="image"]').forEach(meta =>
    push(meta.getAttribute('content'), 'img', 0, 0, 'preview', { meta: true }));

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
    // The poster is a page-level preview, often unrelated to any frame. List it as its
    // OWN image item (openable/croppable independently) and tag the video item with it.
    // Fall back to the probe's persisted snapshot (`__stencilPoster`, same extension
    // isolated world) since some players strip the live poster attribute on playback.
    const rawPoster = v.poster || v.__stencilPoster || '';
    const poster = rawPoster ? abs(rawPoster) : '';
    if (poster) {
      // The same image is often ALSO a plain <img> (scanned first) — tag that existing
      // row as a poster rather than duplicating; otherwise add the poster as its own item.
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
    for (const pseudo of PSEUDOS) {
      let cs;
      try { cs = getComputedStyle(el, pseudo); } catch { continue; }
      for (const prop of CSS_IMG_PROPS) {
        for (const u of cssImageUrls(cs[prop])) push(u, 'bg', 0, 0, '');
      }
    }
  }

  // Web-app-manifest icons — the only source that needs an async fetch (+ JSON parse).
  // Best-effort: a missing/cross-origin/malformed manifest is silently skipped.
  const manifestLink = document.querySelector('link[rel~="manifest"]');
  if (manifestLink && out.length < limit) {
    try {
      const manifestUrl = abs(manifestLink.getAttribute('href'));
      const res = await fetch(manifestUrl, { credentials: manifestLink.crossOrigin ? 'omit' : 'include' });
      const manifest = await res.json();
      for (const ic of (Array.isArray(manifest.icons) ? manifest.icons : [])) {
        if (ic && ic.src) push(abs(new URL(ic.src, manifestUrl).href), 'img', 0, 0, ic.purpose || 'app icon', { meta: true });
      }
    } catch { /* no manifest / blocked / bad JSON */ }
  }

  return out;
};
