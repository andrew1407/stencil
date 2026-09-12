// window.stencil in every page's MAIN world, only while options → "Page scripting API" is on.
// MAIN world so entries carry live DOM elements; without chrome.* there, action requests are
// postMessage'd to the ISOLATED bridge (content/pageApiBridge.js). Classic script — no import;
// the pure helpers mirror lib/pageImages.js (pageApiMainMirror.test.js pins them).
(() => {
  // Never clobber the editor's OWN window.stencil (no __stencil tag): the editor page wins.
  if (window.stencil) {
    if (window.stencil.__stencil === 'page') return;
    if (!window.stencil.__stencil) return;
  }

  // mirror of lib/messages.js (MAIN-world script — can't import)
  const MSG = { PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop', PAGE_PIN: 'stencil-page-pin', PAGE_REQUEST_SYNC: 'stencil-page-request-sync', PAGE_DISABLE: 'stencil-page-disable', PAGE_SET_FILTERS: 'stencil-page-set-filters' };
  const SRC = { PAGE_API: 'stencil-page-api', PAGE_FILTERS: 'stencil-page-filters', PAGE_PINS: 'stencil-page-pins', PAGE_EDITED: 'stencil-page-edited', PAGE_HL_COLOR: 'stencil-page-hl-color' };

  // Pinned (this site) / opened-in-an-editor source URLs, pushed live by the bridge; a pin
  // write updates pinnedSources optimistically so the getter flips at once.
  const pinnedSources = new Set();
  const editedSources = new Set();
  let hlColor = '#7c3aed';

  const send = (message) => window.postMessage({ source: SRC.PAGE_API, message }, '*');

  const bgImageUrl = (cssValue) => {
    const m = /url\((['"]?)(.*?)\1\)/i.exec(String(cssValue || ''));
    const url = m ? m[2].trim() : '';
    return url || '';
  };
  // Inline mirror of lib/pageImages.js cssImageUrls (pageApiMainMirror.test.js).
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
    } catch { return `${fallback}.png`; }
  };
  const videoHasFrame = (v) => !!(v && v.videoWidth && v.videoHeight && v.readyState >= 2 && !(v.paused && !v.currentTime));

  // Lowercase media format from a URL / data: URI ('' if unknown) — mirrors lib/filters.js.
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

  // CSS properties whose value can hold an image url() — mirrors imageScan.js.
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

  // Hard-guard: a real setter writes through; writing a method / read-only getter / data
  // field (or adding / deleting one) THROWS instead of silently no-opping.
  const guard = (obj) => new Proxy(Object.freeze(obj), {
    set(target, prop, value) {
      const d = Object.getOwnPropertyDescriptor(target, prop);
      if (d && typeof d.set === 'function') { d.set.call(target, value); return true; }
      throw new TypeError(`stencil: "${String(prop)}" is read-only and cannot be reassigned`);
    },
    defineProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" is read-only`); },
    deleteProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" cannot be deleted`); },
  });

  // Optimistic: pinnedSources flips before the bridge → SW write lands. Throws without an
  // openable URL to key the pin on (mirrors open()/resolveTarget).
  const setPinnedState = (entry, on) => {
    const url = entry && entry.url;
    if (!url) throw new Error('Stencil: nothing to pin — this item has no openable source URL');
    if (on) pinnedSources.add(url); else pinnedSources.delete(url);
    send({ type: MSG.PAGE_PIN, pin: !!on, url, source: url, name: entry.name, kind: entry.kind, resource: location.href });
  };

  const makeEntry = (el, kind, url, poster = false, meta = false) => guard({
    __stencilEntry: true,
    element: el,
    kind,
    url,
    poster,
    // Favicon / og: <meta> / manifest icon / preload — gated by "Icons & metadata", out of `images`.
    meta,
    get name() { return nameFromUrl(url, kind === 'video' ? 'video' : 'image'); },
    get format() { return formatOf(url); },
    get width() { return entryDims(el, kind).w; },
    get height() { return entryDims(el, kind).h; },
    get pinned() { return pinnedSources.has(url); },
    set pinned(v) { setPinnedState(this, !!v); },
    get isEdited() { return editedSources.has(url); },
    open(opts) { return api.open(this, opts); },
    crop(opts) { return api.crop(this, opts); },
    pin() { setPinnedState(this, true); return this; },
    unpin() { setPinnedState(this, false); return this; },
  });

  // Absolutised against the page URL (a favicon/meta ref is often relative); data:/blob: pass through.
  const absUrl = (raw) => { const s = raw && String(raw).trim(); if (!s) return ''; try { return new URL(s, location.href).href; } catch { return s; } };

  // A prefetch <link> has no `as`, so only treat it as an image when its href clearly is one.
  const IMG_EXT = /\.(png|jpe?g|gif|webp|avif|bmp|ico|cur|svg|tiff?)(?:[?#]|$)/i;

  // Deduped by absolute URL: one <img srcset> lists several alternates, a poster that is
  // also a plain <img> collapses to one. Bounded element walk for CSS images.
  const scan = () => {
    const out = [], seen = new Set();
    const add = (el, kind, raw, meta = false) => { const url = absUrl(raw); if (!url || seen.has(url)) return; seen.add(url); out.push(makeEntry(el, kind, url, false, meta)); };
    document.querySelectorAll('img').forEach((el) => add(el, 'image', el.currentSrc || el.getAttribute('src') || ''));
    document.querySelectorAll('img[srcset], source[srcset]').forEach((el) => srcsetUrls(el.getAttribute('srcset')).forEach((u) => add(el, 'image', u)));
    document.querySelectorAll('image, feImage').forEach((el) => add(el, 'image', el.getAttribute('href') || el.getAttribute('xlink:href') || ''));
    document.querySelectorAll('input[type="image"]').forEach((el) => add(el, 'image', el.currentSrc || el.getAttribute('src') || ''));
    document.querySelectorAll('video').forEach((el) => add(el, 'video', el.currentSrc || el.getAttribute('src') || el.getAttribute('poster') || ''));
    document.querySelectorAll('link[rel~="icon"], link[rel="apple-touch-icon"], link[rel="apple-touch-icon-precomposed"], link[rel="mask-icon"], link[rel="preload"][as="image"], link[rel="prefetch"]').forEach((el) => {
      const rel = (el.getAttribute('rel') || '').toLowerCase();
      const href = el.getAttribute('href') || '';
      if (href && (!rel.includes('prefetch') || IMG_EXT.test(href))) add(el, 'image', href, true);
      srcsetUrls(el.getAttribute('imagesrcset')).forEach((u) => add(el, 'image', u, true));
    });
    document.querySelectorAll('meta[property="og:image"], meta[property="og:image:url"], meta[property="og:image:secure_url"], meta[name="twitter:image"], meta[name="twitter:image:src"], meta[itemprop="image"]').forEach((el) => add(el, 'image', el.getAttribute('content') || '', true));
    const all = document.querySelectorAll('*');
    for (let i = 0; i < all.length && i < 8000; i++) {
      const el = all[i];
      for (const pseudo of PSEUDOS) {
        let cs;
        try { cs = getComputedStyle(el, pseudo); } catch { continue; }
        for (const prop of CSS_IMG_PROPS) for (const u of cssImageUrls(cs[prop])) add(el, 'background', u);
      }
    }
    return out;
  };

  // Live filter state, two-way bound to the popup's controls via chrome.storage.local.popupFilters
  // (the bridge proxies storage). One-off queries search()/format()/size() stay unfiltered.
  const filters = {
    searchText: '',
    regex: false,                   // treat searchText as a case-insensitive RegExp (stencil.regex)
    disabledFormats: new Set(),     // lowercase formats toggled off via stencil.formats.<f> = false
    image: true, background: true, video: true, poster: true, meta: true,   // kind toggles (stencil.kinds.<k>)
    minWidth: null, maxWidth: null, minHeight: null, maxHeight: null,
  };
  const isNum = (v) => typeof v === 'number' && !isNaN(v);
  // Mirrors lib/filters.js matchesSearch; the query stays raw so regex metacharacters survive.
  const searchMatch = (hay, query, regex) => {
    if (!query) return true;
    if (regex) { let re; try { re = new RegExp(query, 'i'); } catch { return false; } return re.test(hay); }
    return hay.toLowerCase().includes(query.toLowerCase());
  };
  const passes = (e) => {
    if (e.poster) { if (!filters.poster) return false; }
    else if (e.meta) { if (!filters.meta) return false; }        // icon / metadata toggle
    else if (!filters[e.kind]) return false;                     // kind toggle: image / background / video
    if (filters.searchText && !searchMatch(`${e.name} ${e.url}`, filters.searchText, filters.regex)) return false;
    if (e.format && filters.disabledFormats.has(e.format)) return false;
    if (e.width > 0) { if (isNum(filters.minWidth) && e.width < filters.minWidth) return false; if (isNum(filters.maxWidth) && e.width > filters.maxWidth) return false; }
    if (e.height > 0) { if (isNum(filters.minHeight) && e.height < filters.minHeight) return false; if (isNum(filters.maxHeight) && e.height > filters.maxHeight) return false; }
    return true;
  };
  const scanFiltered = () => scan().filter(passes);
  const scanPosters = () => {
    const out = [];
    document.querySelectorAll('video').forEach((v) => {
      const p = v.getAttribute('poster') || '';
      if (p) out.push(makeEntry(v, 'image', p, true));
    });
    return out;
  };

  // Shares the popup's <style id=stencil-hl-style> + data-stencil-hl attr, so toggling here is
  // detected by the popup and vice versa (lib/highlight.js).
  const HL_STYLE_ID = 'stencil-hl-style';
  const HL_ATTR = 'data-stencil-hl';
  const HL_HOVER = 'data-stencil-hl-hover';
  const highlightActive = () => !!document.getElementById(HL_STYLE_ID);
  const hlToRgb = (hex) => {
    let h = String(hex || '').trim().replace('#', '');
    if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
    const n = parseInt(h, 16);
    return Number.isFinite(n) && h.length === 6 ? { r: (n >> 16) & 255, g: (n >> 8) & 255, b: n & 255 } : { r: 124, g: 58, b: 237 };
  };
  const hlStyleText = () => {
    const { r, g, b } = hlToRgb(hlColor);
    const lift = (v) => Math.round(v + (255 - v) * 0.28);
    const hov = `rgb(${lift(r)},${lift(g)},${lift(b)})`, glow = `rgba(${lift(r)},${lift(g)},${lift(b)},.45)`;
    return '[' + HL_ATTR + ']{outline:2px solid ' + hlColor + ' !important;outline-offset:-2px !important;' +
      'transition:outline-color .16s ease,outline-offset .16s ease,box-shadow .18s ease !important;}' +
      '[' + HL_HOVER + ']{outline:3px solid ' + hov + ' !important;outline-offset:-3px !important;box-shadow:0 0 0 3px ' + glow + ' !important;}';
  };
  const hlAt = (start) => { for (let n = start; n && n.nodeType === 1; n = n.parentElement) if (n.hasAttribute && n.hasAttribute(HL_ATTR)) return n; return null; };
  let hlCurrent = null, hlBound = false;
  const hlOnOver = (e) => {
    const t = hlAt(e.target);
    if (t === hlCurrent) return;
    if (hlCurrent) hlCurrent.removeAttribute(HL_HOVER);
    hlCurrent = t;
    if (hlCurrent) hlCurrent.setAttribute(HL_HOVER, '');
  };
  const applyHighlight = () => {
    document.querySelectorAll('[' + HL_ATTR + ']').forEach((el) => el.removeAttribute(HL_ATTR));
    if (!document.getElementById(HL_STYLE_ID)) {
      const style = document.createElement('style');
      style.id = HL_STYLE_ID;
      style.textContent = hlStyleText();
      (document.head || document.documentElement).appendChild(style);
    }
    // Cleanup teardown is shared with the popup via window.__stencilHlCleanup.
    if (!hlBound && typeof document.addEventListener === 'function') {
      document.addEventListener('mouseover', hlOnOver, true);
      hlBound = true;
      window.__stencilHlCleanup = () => {
        document.removeEventListener('mouseover', hlOnOver, true);
        if (hlCurrent) hlCurrent.removeAttribute(HL_HOVER);
        hlCurrent = null; hlBound = false;
      };
    }
    for (const e of scanFiltered()) { const el = e.element; if (el && el.setAttribute) el.setAttribute(HL_ATTR, ''); }
  };
  const recolorHighlight = () => { const s = document.getElementById(HL_STYLE_ID); if (s) s.textContent = hlStyleText(); };
  const clearHighlight = () => {
    document.querySelectorAll('[' + HL_ATTR + ']').forEach((el) => el.removeAttribute(HL_ATTR));
    document.querySelectorAll('[' + HL_HOVER + ']').forEach((el) => el.removeAttribute(HL_HOVER));
    const style = document.getElementById(HL_STYLE_ID); if (style) style.remove();
    try { if (typeof window.__stencilHlCleanup === 'function') { window.__stencilHlCleanup(); window.__stencilHlCleanup = null; } } catch { /* ignore */ }
  };

  let syncing = false;   // true while applying a pushed update, so we don't echo it back
  const toPopupShape = () => ({
    search: filters.searchText, regex: filters.regex, minW: filters.minWidth, maxW: filters.maxWidth, minH: filters.minHeight, maxH: filters.maxHeight,
    includeImg: filters.image, includeBg: filters.background, includeVideo: filters.video, includePosters: filters.poster, includeMeta: filters.meta,
    disabledFormats: [...filters.disabledFormats],
  });
  const fromPopupShape = (f) => {
    if (!f) return;
    filters.searchText = String(f.search || '');
    filters.regex = f.regex === true;
    filters.minWidth = f.minW ?? null; filters.maxWidth = f.maxW ?? null; filters.minHeight = f.minH ?? null; filters.maxHeight = f.maxH ?? null;
    filters.image = f.includeImg !== false; filters.background = f.includeBg !== false;
    filters.video = f.includeVideo !== false; filters.poster = f.includePosters !== false;
    filters.meta = f.includeMeta !== false;
    filters.disabledFormats = new Set(Array.isArray(f.disabledFormats) ? f.disabledFormats : []);
  };
  const persistFilters = () => { if (!syncing) try { send({ type: MSG.PAGE_SET_FILTERS, filters: toPopupShape() }); } catch { /* bridge gone */ } };
  const onFilterChange = () => { if (highlightActive()) applyHighlight(); persistFilters(); };
  const resetSet = (set, sources) => { set.clear(); for (const s of (Array.isArray(sources) ? sources : [])) if (s) set.add(s); };
  window.addEventListener('message', (e) => {
    if (e.source !== window) return;
    const d = e.data;
    if (!d) return;
    if (d.source === SRC.PAGE_PINS) { resetSet(pinnedSources, d.sources); return; }
    if (d.source === SRC.PAGE_EDITED) { resetSet(editedSources, d.sources); return; }
    if (d.source === SRC.PAGE_HL_COLOR) {
      const c = typeof d.color === 'string' && d.color ? d.color : hlColor;
      if (c !== hlColor) {
        hlColor = c;
        if (highlightActive()) recolorHighlight();
      }
      return;
    }
    if (d.source !== SRC.PAGE_FILTERS) return;
    syncing = true;
    try { fromPopupShape(d.filters); } finally { syncing = false; }
    if (highlightActive()) applyHighlight();
  });

  // { <format>: boolean } for stencil.formats — assigning false hides that format from the lists.
  const formatsToggle = () => {
    const obj = {};
    for (const f of [...new Set(scan().map((e) => e.format).filter(Boolean))].sort()) {
      Object.defineProperty(obj, f, {
        enumerable: true, configurable: true,
        get: () => !filters.disabledFormats.has(f),
        set: (v) => { v ? filters.disabledFormats.delete(f) : filters.disabledFormats.add(f); onFilterChange(); },
      });
    }
    return obj;
  };

  const kindsToggle = () => {
    const obj = {};
    for (const k of ['image', 'background', 'video', 'poster', 'meta']) Object.defineProperty(obj, k, {
      enumerable: true, configurable: true,
      get: () => filters[k],
      set: (v) => { filters[k] = !!v; onFilterChange(); },
    });
    return obj;
  };

  // Throws when nothing loadable is found (mirrors the popup's editableSrc/sourceOf).
  const resolveTarget = (target, opts = {}) => {
    let el = null, kind = null, url = '';
    if (target && target.__stencilEntry) { el = target.element; kind = target.kind; url = target.url; }
    else if (target && target.nodeType === 1) { const r = elementUrl(target); el = target; kind = r.kind; url = r.url; }
    else if (typeof target === 'string' && target) return { url: target, name: nameFromUrl(target), source: target };
    else throw new Error('Stencil: pass an image/video element, a scanned entry, or an image URL');

    if (kind === 'video') {
      if (!opts.poster) {
        const frame = captureVideoFrame(el);
        if (frame) return { dataUrl: frame, name: nameFromUrl(el.currentSrc || el.src || 'video', 'video'), source: el.currentSrc || el.src || '' };
      }
      const poster = el.getAttribute('poster') || '';
      if (poster) return { url: poster, name: nameFromUrl(poster), source: poster };
      throw new Error('Stencil: this video has no readable frame or poster (try { poster: true })');
    }
    if (!url) throw new Error('Stencil: element is not a loadable image');
    return { url, name: nameFromUrl(url), source: url };
  };

  // A pin keys on the source URL, not a frame — no video-frame capture here.
  const resolvePinTargets = (target) => {
    if (Array.isArray(target)) return target.flatMap(resolvePinTargets);
    if (typeof target === 'number') { const e = scanFiltered()[target]; return e ? [e] : []; }
    if (target && target.__stencilEntry) return [target];
    if (target && target.nodeType === 1) {
      const r = elementUrl(target);
      return r.url ? [{ url: r.url, name: nameFromUrl(r.url, r.kind === 'video' ? 'video' : 'image'), kind: r.kind || 'image' }] : [];
    }
    if (typeof target === 'string' && target) return [{ url: target, name: nameFromUrl(target), kind: 'image' }];
    throw new Error('Stencil: pin() expects an entry, an item index, an element, a URL, or an array of those');
  };

  // Only labels detect()'s `kind` for a raw URL; element/entry targets carry their own kind.
  const VIDEO_FMTS = new Set(['mp4', 'webm', 'mov', 'm4v', 'mkv', 'avi', 'ogv', 'ogg']);

  // Never throws: null when the target carries nothing grabbable.
  const describeTarget = (target) => {
    let el = null, kind = null, url = '';
    let listing = null;                                   // scanFiltered() result, computed at most once
    const entries = () => (listing || (listing = scanFiltered()));
    if (target && target.__stencilEntry) { el = target.element; kind = target.kind; url = target.url; }
    else if (typeof target === 'number') { const e = entries()[target]; if (!e) return null; el = e.element; kind = e.kind; url = e.url; }
    else if (target && target.nodeType === 1) { const r = elementUrl(target); el = target; kind = r.kind; url = r.url; }
    else if (typeof target === 'string' && target) { kind = VIDEO_FMTS.has(formatOf(target)) ? 'video' : 'image'; url = target; }
    else return null;

    const hasFrame = kind === 'video' && videoHasFrame(el);
    const hasPoster = !!(el && el.nodeType === 1 && el.getAttribute && el.getAttribute('poster'));
    // Grabbable = there's an openable source URL, or a <video> we can capture a frame from.
    if (!url && !hasFrame) return null;
    return {
      kind, url, element: el, hasFrame, hasPoster,
      name: nameFromUrl(url || (el && (el.currentSrc || el.src)) || '', kind === 'video' ? 'video' : 'image'),
      format: formatOf(url),
      pinned: !!url && pinnedSources.has(url),
      isEdited: !!url && editedSources.has(url),
      listed: entries().some((e) => (el && e.element === el) || (!!url && e.url === url)),
    };
  };

  const api = {
    __stencil: 'page',
    get enabled() { return true; },
    set enabled(v) { if (!v) send({ type: MSG.PAGE_DISABLE }); },
    get items() { return scanFiltered(); },
    get images() { return scanFiltered().filter((e) => e.kind === 'image' && !e.meta); },
    get backgrounds() { return scanFiltered().filter((e) => e.kind === 'background'); },
    get icons() { return scanFiltered().filter((e) => e.meta); },
    get videos() { return scanFiltered().filter((e) => e.kind === 'video'); },
    get pins() { return scanFiltered().filter((e) => e.pinned); },
    get posters() { return scanPosters().filter(passes); },
    // stencil.formats.png = false hides that format; keys are the formats present on the page.
    get formats() { return formatsToggle(); },
    get kinds() { return kindsToggle(); },
    get searchText() { return filters.searchText; }, set searchText(v) { filters.searchText = String(v || ''); onFilterChange(); },
    get regex() { return filters.regex; }, set regex(v) { filters.regex = !!v; onFilterChange(); },
    get minWidth() { return filters.minWidth; }, set minWidth(v) { filters.minWidth = v == null ? null : Number(v); onFilterChange(); },
    get maxWidth() { return filters.maxWidth; }, set maxWidth(v) { filters.maxWidth = v == null ? null : Number(v); onFilterChange(); },
    get minHeight() { return filters.minHeight; }, set minHeight(v) { filters.minHeight = v == null ? null : Number(v); onFilterChange(); },
    get maxHeight() { return filters.maxHeight; }, set maxHeight(v) { filters.maxHeight = v == null ? null : Number(v); onFilterChange(); },
    // Shares the popup's highlight element so the two stay in sync; `highlightOnPage` is an alias.
    get highlightOnImage() { return highlightActive(); }, set highlightOnImage(v) { v ? applyHighlight() : clearHighlight(); },
    get highlightOnPage() { return highlightActive(); }, set highlightOnPage(v) { v ? applyHighlight() : clearHighlight(); },
    resetFilters() {
      filters.searchText = ''; filters.regex = false; filters.disabledFormats.clear();
      filters.image = filters.background = filters.video = filters.poster = filters.meta = true;
      filters.minWidth = filters.maxWidth = filters.minHeight = filters.maxHeight = null;
      clearHighlight();
      persistFilters();
      return this;
    },
    // Ignores the live filters. opts.regex: case-insensitive RegExp (invalid → no matches).
    search(q, opts = {}) {
      const query = String(q || '');
      if (!query) return scan();
      return scan().filter((e) => searchMatch(`${e.name} ${e.url}`, query, !!opts.regex));
    },
    format(fmt) {
      const want = normFmt(String(fmt || '').replace(/^\./, ''));
      return want ? scan().filter((e) => e.format === want) : [];
    },
    // Unknown size (0, e.g. an unloaded background) passes, exactly as the popup's size filter does.
    size({ minW, maxW, minH, maxH } = {}) {
      return scan().filter((e) => {
        if (e.width > 0) { if (isNum(minW) && e.width < minW) return false; if (isNum(maxW) && e.width > maxW) return false; }
        if (e.height > 0) { if (isNum(minH) && e.height < minH) return false; if (isNum(maxH) && e.height > maxH) return false; }
        return true;
      });
    },
    // opts: { incognito, newTab, desktop, poster, frame }; desktop needs a configured stencil:// scheme.
    open(target, opts = {}) {
      const r = resolveTarget(target, opts);
      send({ type: MSG.PAGE_OPEN, url: r.url, dataUrl: r.dataUrl, name: r.name, source: r.source, resource: location.href, incognito: !!opts.incognito, newTab: !!opts.newTab, desktop: !!opts.desktop });
      return this;
    },
    crop(target, opts = {}) {
      const r = resolveTarget(target, opts);
      send({ type: MSG.PAGE_CROP, url: r.url, dataUrl: r.dataUrl, source: r.source, resource: location.href, album: !!opts.album });
      return this;
    },
    // Chainable; accepts an entry, a stencil.items index, an element, a URL, or an array of those.
    pin(target) { for (const t of resolvePinTargets(target)) setPinnedState(t, true); return this; },
    unpin(target) { for (const t of resolvePinTargets(target)) setPinnedState(t, false); return this; },
    // → { kind, url, name, format, element, hasFrame, hasPoster, pinned, isEdited, listed } or null.
    detect(target) { return describeTarget(target); },
    grabbable(target) { return !!describeTarget(target); },
  };

  // Non-enumerable so the console shows a clean `stencil`. Must run before guard()'s freeze
  // locks the descriptors; __stencil stays a property (the back-off guard reads it).
  for (const k of Reflect.ownKeys(api)) {
    const d = Object.getOwnPropertyDescriptor(api, k);
    if (d.enumerable) Object.defineProperty(api, k, { ...d, enumerable: false });
  }
  // The only legit setter is `enabled`; methods `return this` so chaining holds. guard() freezes.
  const guarded = guard(api);

  // configurable:true on purpose: on the editor's own page its non-configurable window.stencil
  // must be able to take over (the back-off guard above already yields to it).
  try {
    Object.defineProperty(window, 'stencil', { value: guarded, writable: false, configurable: true, enumerable: false });
  } catch { /* a non-configurable window.stencil already exists (the editor) — leave it */ }

  // The bridge pushed once at document_start, before this script ran at document_idle — ask
  // for pins / edited / filters / highlight colour again now that the listener exists.
  try { send({ type: MSG.PAGE_REQUEST_SYNC }); } catch { /* bridge not present */ }
})();
