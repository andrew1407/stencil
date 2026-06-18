// ── Page-global window.stencil (MAIN world) ─────────────────────────────────
// Injected into every page's MAIN world ONLY when the user opts in (options →
// "Page scripting API"). It exposes a console-friendly API to scan the page's
// images/videos and send them to the Stencil editor — mirroring the popup/context
// menu. It runs in the MAIN world so returned entries can carry LIVE DOM elements;
// since the MAIN world has no chrome.* APIs, action requests are postMessage'd to
// the ISOLATED bridge (content/pageApiBridge.js), which relays them to the SW.
//
// The pure helpers below MIRROR lib/pageImages.js (the unit-tested source of truth)
// — keep the two in sync.
(() => {
  // Don't double-inject, and never clobber the editor's OWN window.stencil (that API
  // has no __stencil tag; the editor page wins on its own origin).
  if (window.stencil) {
    if (window.stencil.__stencil === 'page') return;
    if (!window.stencil.__stencil) return;
  }

  // mirror of lib/messages.js (MAIN-world script — can't import)
  const MSG = { PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop', PAGE_DISABLE: 'stencil-page-disable', PAGE_SET_FILTERS: 'stencil-page-set-filters' };
  const SRC = { PAGE_API: 'stencil-page-api', PAGE_FILTERS: 'stencil-page-filters' };

  const send = (message) => window.postMessage({ source: SRC.PAGE_API, message }, '*');

  const bgImageUrl = (cssValue) => {
    const m = /url\((['"]?)(.*?)\1\)/i.exec(String(cssValue || ''));
    const url = m ? m[2].trim() : '';
    return (!url || url.startsWith('data:image/svg')) ? '' : url;
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

  // Lowercase media "format" from a URL / data: URI ('' if unknown) — mirrors lib/filters.js.
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

  const elementUrl = (el) => {
    if (!el || el.nodeType !== 1) return { kind: null, url: '' };
    const tag = (el.tagName || '').toLowerCase();
    if (tag === 'img') return { kind: 'image', url: el.currentSrc || el.getAttribute('src') || '' };
    if (tag === 'image') return { kind: 'image', url: el.getAttribute('href') || el.getAttribute('xlink:href') || '' };
    if (tag === 'video') return { kind: 'video', url: el.currentSrc || el.getAttribute('src') || el.getAttribute('poster') || '' };
    try { const bg = bgImageUrl(getComputedStyle(el).backgroundImage); if (bg) return { kind: 'background', url: bg }; } catch { /* cross-origin sheet */ }
    return { kind: null, url: '' };
  };

  // Intrinsic pixel size where the DOM exposes it synchronously: <video> from its
  // decoded dimensions, <img>/<svg image> from naturalWidth. A CSS background has no
  // intrinsic size available without loading it (the popup measures lazily), so we
  // fall back to the element's rendered box — 0 when nothing is known.
  const entryDims = (el, kind) => {
    if (kind === 'video') return { w: el.videoWidth || 0, h: el.videoHeight || 0 };
    if (el && el.naturalWidth) return { w: el.naturalWidth, h: el.naturalHeight || 0 };
    return { w: (el && el.offsetWidth) || 0, h: (el && el.offsetHeight) || 0 };
  };

  // Hard-guard an API object: a property with a real setter writes through, but writing a
  // method / read-only getter / data field (or adding/deleting one) THROWS rather than
  // silently no-opping. Applied to the facade and every scanned entry, so `entry.open = 0`,
  // `entry.url = 'x'`, or `stencil.search = 1` is rejected outright.
  const guard = (obj) => new Proxy(Object.freeze(obj), {
    set(target, prop, value) {
      const d = Object.getOwnPropertyDescriptor(target, prop);
      if (d && typeof d.set === 'function') { d.set.call(target, value); return true; }
      throw new TypeError(`stencil: "${String(prop)}" is read-only and cannot be reassigned`);
    },
    defineProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" is read-only`); },
    deleteProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" cannot be deleted`); },
  });

  const makeEntry = (el, kind, url, poster = false) => guard({
    __stencilEntry: true,
    element: el,
    kind,
    url,
    poster,
    get name() { return nameFromUrl(url, kind === 'video' ? 'video' : 'image'); },
    get format() { return formatOf(url); },
    get width() { return entryDims(el, kind).w; },
    get height() { return entryDims(el, kind).h; },
    open(opts) { return api.open(this, opts); },
    crop(opts) { return api.crop(this, opts); },
  });

  // Scan the page → entry objects (live elements). Bounded element walk for backgrounds.
  const scan = () => {
    const out = [], seen = new Set();
    const add = (el, kind, url) => { if (url && !seen.has(el)) { seen.add(el); out.push(makeEntry(el, kind, url)); } };
    document.querySelectorAll('img').forEach((el) => add(el, 'image', el.currentSrc || el.getAttribute('src') || ''));
    document.querySelectorAll('image').forEach((el) => add(el, 'image', el.getAttribute('href') || el.getAttribute('xlink:href') || ''));
    document.querySelectorAll('video').forEach((el) => add(el, 'video', el.currentSrc || el.getAttribute('src') || el.getAttribute('poster') || ''));
    const all = document.querySelectorAll('*');
    for (let i = 0; i < all.length && i < 8000; i++) {
      const el = all[i];
      if (seen.has(el)) continue;
      let bg = '';
      try { bg = bgImageUrl(getComputedStyle(el).backgroundImage); } catch { /* ignore */ }
      if (bg) add(el, 'background', bg);
    }
    return out;
  };

  // ── Live filter state (mirrors — and SYNCS with — the popup's filter controls) ──
  // The list getters (items/images/videos/backgrounds/posters) honor it; the one-off
  // query methods search()/format()/size() stay unfiltered (explicit ad-hoc queries).
  // It's two-way bound to the popup via chrome.storage.local.popupFilters (the bridge
  // proxies storage for this MAIN-world script — see content/pageApiBridge.js).
  const filters = {
    searchText: '',
    disabledFormats: new Set(),     // lowercase formats toggled off via stencil.formats.<f> = false
    image: true, background: true, video: true, poster: true,   // kind toggles (stencil.kinds.<k>)
    minWidth: null, maxWidth: null, minHeight: null, maxHeight: null,
  };
  const isNum = (v) => typeof v === 'number' && !isNaN(v);
  const passes = (e) => {
    if (e.poster) { if (!filters.poster) return false; }
    else if (!filters[e.kind]) return false;                     // kind toggle: image / background / video
    if (filters.searchText && !`${e.name} ${e.url}`.toLowerCase().includes(filters.searchText)) return false;
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

  // ── Highlight: share the popup's <style id=stencil-hl-style> + data-stencil-hl attr,
  //    so toggling it here is detected by the popup (and vice versa). ──
  const HL_STYLE_ID = 'stencil-hl-style';
  const HL_ATTR = 'data-stencil-hl';
  const highlightActive = () => !!document.getElementById(HL_STYLE_ID);
  const applyHighlight = () => {
    document.querySelectorAll('[' + HL_ATTR + ']').forEach((el) => el.removeAttribute(HL_ATTR));
    if (!document.getElementById(HL_STYLE_ID)) {
      const style = document.createElement('style');
      style.id = HL_STYLE_ID;
      style.textContent = '[' + HL_ATTR + ']{outline:2px solid #7c3aed !important;outline-offset:-2px !important;}';
      (document.head || document.documentElement).appendChild(style);
    }
    for (const e of scanFiltered()) { const el = e.element; if (el && el.setAttribute) el.setAttribute(HL_ATTR, ''); }
  };
  const clearHighlight = () => {
    document.querySelectorAll('[' + HL_ATTR + ']').forEach((el) => el.removeAttribute(HL_ATTR));
    const style = document.getElementById(HL_STYLE_ID); if (style) style.remove();
    try { if (typeof window.__stencilHlCleanup === 'function') { window.__stencilHlCleanup(); window.__stencilHlCleanup = null; } } catch { /* ignore */ }
  };

  // ── Two-way sync with the popup's persisted filters (chrome.storage popupFilters) ──
  let syncing = false;   // true while applying a pushed update, so we don't echo it back
  const toPopupShape = () => ({
    search: filters.searchText, minW: filters.minWidth, maxW: filters.maxWidth, minH: filters.minHeight, maxH: filters.maxHeight,
    includeImg: filters.image, includeBg: filters.background, includeVideo: filters.video, includePosters: filters.poster,
    disabledFormats: [...filters.disabledFormats],
  });
  const fromPopupShape = (f) => {
    if (!f) return;
    filters.searchText = String(f.search || '').toLowerCase();
    filters.minWidth = f.minW ?? null; filters.maxWidth = f.maxW ?? null; filters.minHeight = f.minH ?? null; filters.maxHeight = f.maxH ?? null;
    filters.image = f.includeImg !== false; filters.background = f.includeBg !== false;
    filters.video = f.includeVideo !== false; filters.poster = f.includePosters !== false;
    filters.disabledFormats = new Set(Array.isArray(f.disabledFormats) ? f.disabledFormats : []);
  };
  const persistFilters = () => { if (!syncing) try { send({ type: MSG.PAGE_SET_FILTERS, filters: toPopupShape() }); } catch { /* bridge gone */ } };
  // Called after any filter mutation: refresh the highlight (if on) and persist (→ popup).
  const onFilterChange = () => { if (highlightActive()) applyHighlight(); persistFilters(); };
  // The bridge pushes the stored popup filters on load and whenever they change.
  window.addEventListener('message', (e) => {
    if (e.source !== window) return;
    const d = e.data;
    if (!d || d.source !== SRC.PAGE_FILTERS) return;
    syncing = true;
    try { fromPopupShape(d.filters); } finally { syncing = false; }
    if (highlightActive()) applyHighlight();
  });

  // Live { <format>: boolean } map for stencil.formats — keys are the lowercase formats
  // present on the page; assigning false disables that format in the list getters.
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

  // Live { image, background, video, poster: boolean } map for stencil.kinds — assigning
  // false hides that category from the list getters (the popup's include-* checkboxes).
  const kindsToggle = () => {
    const obj = {};
    for (const k of ['image', 'background', 'video', 'poster']) Object.defineProperty(obj, k, {
      enumerable: true, configurable: true,
      get: () => filters[k],
      set: (v) => { filters[k] = !!v; onFilterChange(); },
    });
    return obj;
  };

  // Validate a target (entry | element | url) → { url? , dataUrl?, name, source }; throws
  // when nothing loadable is found (mirrors the popup's editableSrc/sourceOf logic).
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

  const api = {
    __stencil: 'page',
    get enabled() { return true; },
    set enabled(v) { if (!v) send({ type: MSG.PAGE_DISABLE }); },
    // Scan of the page's images/videos/backgrounds, honoring the live filters below.
    get items() { return scanFiltered(); },
    // Just the images: <img> + inline <svg><image>.
    get images() { return scanFiltered().filter((e) => e.kind === 'image'); },
    // Just CSS background-image elements.
    get backgrounds() { return scanFiltered().filter((e) => e.kind === 'background'); },
    // Just the <video> elements.
    get videos() { return scanFiltered().filter((e) => e.kind === 'video'); },
    // The poster image of every <video> that declares one.
    get posters() { return scanPosters().filter(passes); },
    // Per-format on/off toggles: stencil.formats.png = false. Object.keys lists the
    // formats present on the page; the list getters honor the toggles.
    get formats() { return formatsToggle(); },
    // Per-category on/off toggles: stencil.kinds.video = false (image/background/video/poster).
    get kinds() { return kindsToggle(); },
    // ── Other filter controls (mirror the popup; the list getters above honor them) ──
    get searchText() { return filters.searchText; }, set searchText(v) { filters.searchText = String(v || '').toLowerCase(); onFilterChange(); },
    get minWidth() { return filters.minWidth; }, set minWidth(v) { filters.minWidth = v == null ? null : Number(v); onFilterChange(); },
    get maxWidth() { return filters.maxWidth; }, set maxWidth(v) { filters.maxWidth = v == null ? null : Number(v); onFilterChange(); },
    get minHeight() { return filters.minHeight; }, set minHeight(v) { filters.minHeight = v == null ? null : Number(v); onFilterChange(); },
    get maxHeight() { return filters.maxHeight; }, set maxHeight(v) { filters.maxHeight = v == null ? null : Number(v); onFilterChange(); },
    // Outline the (currently-filtered) images on the page (get/set). Shares the popup's
    // highlight element, so the two stay in sync. `highlightOnPage` is an alias.
    get highlightOnImage() { return highlightActive(); }, set highlightOnImage(v) { v ? applyHighlight() : clearHighlight(); },
    get highlightOnPage() { return highlightActive(); }, set highlightOnPage(v) { v ? applyHighlight() : clearHighlight(); },
    // Reset every filter and clear the highlight.
    resetFilters() {
      filters.searchText = ''; filters.disabledFormats.clear();
      filters.image = filters.background = filters.video = filters.poster = true;
      filters.minWidth = filters.maxWidth = filters.minHeight = filters.maxHeight = null;
      clearHighlight();
      persistFilters();
      return this;
    },
    // One-off query: scanned entries whose name or URL contains q (ignores the live filters).
    search(q) {
      const s = String(q || '').toLowerCase();
      return scan().filter((e) => `${e.name} ${e.url}`.toLowerCase().includes(s));
    },
    // Entries of a given format (case-insensitive; accepts 'png' or '.png').
    format(fmt) {
      const want = normFmt(String(fmt || '').replace(/^\./, ''));
      return want ? scan().filter((e) => e.format === want) : [];
    },
    // Entries within pixel-size bounds. opts: { minW, maxW, minH, maxH } (any omitted =
    // no bound). Entries of unknown size (0, e.g. an unloaded background) pass, exactly
    // as the popup's size filter does.
    size({ minW, maxW, minH, maxH } = {}) {
      return scan().filter((e) => {
        if (e.width > 0) { if (isNum(minW) && e.width < minW) return false; if (isNum(maxW) && e.width > maxW) return false; }
        if (e.height > 0) { if (isNum(minH) && e.height < minH) return false; if (isNum(maxH) && e.height > maxH) return false; }
        return true;
      });
    },
    // Open a target in the editor. opts: { incognito, newTab, poster, frame }.
    open(target, opts = {}) {
      const r = resolveTarget(target, opts);
      send({ type: MSG.PAGE_OPEN, url: r.url, dataUrl: r.dataUrl, name: r.name, source: r.source, resource: location.href, incognito: !!opts.incognito, newTab: !!opts.newTab });
      return this;
    },
    // Open a target in the quick-crop tool. opts: { album, poster }.
    crop(target, opts = {}) {
      const r = resolveTarget(target, opts);
      send({ type: MSG.PAGE_CROP, url: r.url, dataUrl: r.dataUrl, source: r.source, resource: location.href, album: !!opts.album });
      return this;
    },
  };

  // Hide every member from enumeration so the console shows a clean `stencil` (no
  // __stencil tag / method dump on `console.log(stencil)` or Object.keys). Access and
  // DevTools autocomplete still work — non-enumerable ≠ inaccessible. Must run before
  // the freeze below (freeze locks descriptors). Internal __stencil stays a property
  // (the back-off guard reads it) but no longer leaks into enumeration.
  for (const k of Reflect.ownKeys(api)) {
    const d = Object.getOwnPropertyDescriptor(api, k);
    if (d.enumerable) Object.defineProperty(api, k, { ...d, enumerable: false });
  }
  // Hard-guard with the same proxy as the entries: writing a method, a read-only getter,
  // or the internal __stencil tag THROWS instead of silently no-opping (`stencil.open = 0`
  // / `stencil.__stencil = 'x'`). The only legit setter is `enabled`, which the trap
  // delegates to. Methods `return this` (the proxy when called on it), so chaining holds.
  // (guard() does the Object.freeze the API previously did on its own.)
  const guarded = guard(api);

  // Lock the binding against plain reassignment (writable:false). configurable:true is
  // kept deliberately: on the editor's own page its (non-configurable) window.stencil
  // must be able to take over — and our back-off guard above already yields to it.
  try {
    Object.defineProperty(window, 'stencil', { value: guarded, writable: false, configurable: true, enumerable: false });
  } catch { /* a non-configurable window.stencil already exists (the editor) — leave it */ }
})();
