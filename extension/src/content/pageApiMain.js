// ── Page-global window.stencil (MAIN world) ─────────────────────────────────
// Injected into every page's MAIN world ONLY when opted in (options → "Page scripting
// API"). Console-friendly API to scan the page's images/videos and send them to the
// Stencil editor (mirrors popup/context menu). MAIN world so entries carry LIVE DOM
// elements; lacking chrome.* APIs there, action requests are postMessage'd to the
// ISOLATED bridge (content/pageApiBridge.js), which relays them to the SW.
// The pure helpers below MIRROR lib/pageImages.js (unit-tested source of truth) —
// keep the two in sync.
(() => {
  // Don't double-inject, and never clobber the editor's OWN window.stencil (that API
  // has no __stencil tag; the editor page wins on its own origin).
  if (window.stencil) {
    if (window.stencil.__stencil === 'page') return;
    if (!window.stencil.__stencil) return;
  }

  // mirror of lib/messages.js (MAIN-world script — can't import)
  const MSG = { PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop', PAGE_PIN: 'stencil-page-pin', PAGE_REQUEST_SYNC: 'stencil-page-request-sync', PAGE_DISABLE: 'stencil-page-disable', PAGE_SET_FILTERS: 'stencil-page-set-filters' };
  const SRC = { PAGE_API: 'stencil-page-api', PAGE_FILTERS: 'stencil-page-filters', PAGE_PINS: 'stencil-page-pins', PAGE_EDITED: 'stencil-page-edited', PAGE_HL_COLOR: 'stencil-page-hl-color' };

  // Source URLs the bridge tells us are pinned (on this site) / opened-in-an-editor.
  // entry.pinned / entry.isEdited read these synchronously; the bridge keeps them live
  // (chrome.storage → SRC.PAGE_PINS / SRC.PAGE_EDITED). A pin write optimistically
  // updates pinnedSources so the getter flips before the round-trip lands.
  const pinnedSources = new Set();
  const editedSources = new Set();
  // The highlight outline colour (accent or custom), pushed by the bridge; default violet.
  let hlColor = '#7c3aed';

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

  // Intrinsic pixel size where the DOM exposes it synchronously: <video> from decoded
  // dimensions, <img>/<svg image> from naturalWidth. A CSS background has no intrinsic
  // size without loading it (popup measures lazily), so fall back to the rendered box —
  // 0 when nothing is known.
  const entryDims = (el, kind) => {
    if (kind === 'video') return { w: el.videoWidth || 0, h: el.videoHeight || 0 };
    if (el && el.naturalWidth) return { w: el.naturalWidth, h: el.naturalHeight || 0 };
    return { w: (el && el.offsetWidth) || 0, h: (el && el.offsetHeight) || 0 };
  };

  // Hard-guard an API object: a property with a real setter writes through, but writing
  // a method / read-only getter / data field (or adding/deleting one) THROWS instead of
  // silently no-opping. Applied to the facade and every scanned entry, so `entry.open = 0`,
  // `entry.url = 'x'`, or `stencil.search = 1` is rejected.
  const guard = (obj) => new Proxy(Object.freeze(obj), {
    set(target, prop, value) {
      const d = Object.getOwnPropertyDescriptor(target, prop);
      if (d && typeof d.set === 'function') { d.set.call(target, value); return true; }
      throw new TypeError(`stencil: "${String(prop)}" is read-only and cannot be reassigned`);
    },
    defineProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" is read-only`); },
    deleteProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" cannot be deleted`); },
  });

  // Post a pin / unpin request (→ bridge → SW writes the pin store) and optimistically
  // reflect it locally so entry.pinned reads true/false immediately. Throws when there's
  // no openable URL to key the pin on (mirrors open()/resolveTarget).
  const setPinnedState = (entry, on) => {
    const url = entry && entry.url;
    if (!url) throw new Error('Stencil: nothing to pin — this item has no openable source URL');
    if (on) pinnedSources.add(url); else pinnedSources.delete(url);
    send({ type: MSG.PAGE_PIN, pin: !!on, url, source: url, name: entry.name, kind: entry.kind, resource: location.href });
  };

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
    // Pinned on this site (popup list + options page). Assignable: `entry.pinned = true`.
    get pinned() { return pinnedSources.has(url); },
    set pinned(v) { setPinnedState(this, !!v); },
    // Whether this image was/is opened (edited) in an editor — read-only, from the ledger.
    get isEdited() { return editedSources.has(url); },
    open(opts) { return api.open(this, opts); },
    crop(opts) { return api.crop(this, opts); },
    pin() { setPinnedState(this, true); return this; },
    unpin() { setPinnedState(this, false); return this; },
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
  // List getters (items/images/videos/backgrounds/posters) honor it; one-off queries
  // search()/format()/size() stay unfiltered. Two-way bound to the popup via
  // chrome.storage.local.popupFilters (the bridge proxies storage for this MAIN-world
  // script — see content/pageApiBridge.js).
  const filters = {
    searchText: '',
    regex: false,                   // treat searchText as a case-insensitive RegExp (stencil.regex)
    disabledFormats: new Set(),     // lowercase formats toggled off via stencil.formats.<f> = false
    image: true, background: true, video: true, poster: true,   // kind toggles (stencil.kinds.<k>)
    minWidth: null, maxWidth: null, minHeight: null, maxHeight: null,
  };
  const isNum = (v) => typeof v === 'number' && !isNaN(v);
  // Case-insensitive search over `hay`: a RegExp when `regex` (invalid pattern → no match),
  // else a substring. Mirrors lib/filters.js matchesSearch; the query is kept raw so regex
  // metacharacters (\D, [A-Z]) survive.
  const searchMatch = (hay, query, regex) => {
    if (!query) return true;
    if (regex) { let re; try { re = new RegExp(query, 'i'); } catch { return false; } return re.test(hay); }
    return hay.toLowerCase().includes(query.toLowerCase());
  };
  const passes = (e) => {
    if (e.poster) { if (!filters.poster) return false; }
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

  // ── Highlight: share the popup's <style id=stencil-hl-style> + data-stencil-hl attr,
  //    so toggling it here is detected by the popup (and vice versa). The element under
  //    the cursor gets a thicker, brightened hover ring + glow — same behaviour as the
  //    popup's lib/highlight.js (kept in sync). ──
  const HL_STYLE_ID = 'stencil-hl-style';
  const HL_ATTR = 'data-stencil-hl';
  const HL_HOVER = 'data-stencil-hl-hover';
  const highlightActive = () => !!document.getElementById(HL_STYLE_ID);
  // Hover ring derives from the base colour: same hue brightened toward white, plus a glow.
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
  // Nearest marked ancestor of the cursor target (follows the ring onto a parent bg element).
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
    // Track the cursor so the element under it gets the hover ring (once; cleanup teardown
    // is shared with the popup via window.__stencilHlCleanup, which clearHighlight calls).
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
  // Re-colour an active highlight in place — just rewrite the style rules, no DOM re-scan.
  const recolorHighlight = () => { const s = document.getElementById(HL_STYLE_ID); if (s) s.textContent = hlStyleText(); };
  const clearHighlight = () => {
    document.querySelectorAll('[' + HL_ATTR + ']').forEach((el) => el.removeAttribute(HL_ATTR));
    document.querySelectorAll('[' + HL_HOVER + ']').forEach((el) => el.removeAttribute(HL_HOVER));
    const style = document.getElementById(HL_STYLE_ID); if (style) style.remove();
    try { if (typeof window.__stencilHlCleanup === 'function') { window.__stencilHlCleanup(); window.__stencilHlCleanup = null; } } catch { /* ignore */ }
  };

  // ── Two-way sync with the popup's persisted filters (chrome.storage popupFilters) ──
  let syncing = false;   // true while applying a pushed update, so we don't echo it back
  const toPopupShape = () => ({
    search: filters.searchText, regex: filters.regex, minW: filters.minWidth, maxW: filters.maxWidth, minH: filters.minHeight, maxH: filters.maxHeight,
    includeImg: filters.image, includeBg: filters.background, includeVideo: filters.video, includePosters: filters.poster,
    disabledFormats: [...filters.disabledFormats],
  });
  const fromPopupShape = (f) => {
    if (!f) return;
    filters.searchText = String(f.search || '');
    filters.regex = f.regex === true;
    filters.minWidth = f.minW ?? null; filters.maxWidth = f.maxW ?? null; filters.minHeight = f.minH ?? null; filters.maxHeight = f.maxH ?? null;
    filters.image = f.includeImg !== false; filters.background = f.includeBg !== false;
    filters.video = f.includeVideo !== false; filters.poster = f.includePosters !== false;
    filters.disabledFormats = new Set(Array.isArray(f.disabledFormats) ? f.disabledFormats : []);
  };
  const persistFilters = () => { if (!syncing) try { send({ type: MSG.PAGE_SET_FILTERS, filters: toPopupShape() }); } catch { /* bridge gone */ } };
  // Called after any filter mutation: refresh the highlight (if on) and persist (→ popup).
  const onFilterChange = () => { if (highlightActive()) applyHighlight(); persistFilters(); };
  // Replace a Set's contents with the pushed source-URL list.
  const resetSet = (set, sources) => { set.clear(); for (const s of (Array.isArray(sources) ? sources : [])) if (s) set.add(s); };
  // The bridge pushes the stored popup filters / pinned sources / opened sources on
  // load and whenever they change.
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

  // Resolve a pin/unpin target → array of pin-target objects ({ url, name, kind }) that
  // setPinnedState understands. Accepts a scanned entry, an index into stencil.items, a
  // DOM element, an image/video URL, or an array of any of those. More lenient than
  // resolveTarget (no video-frame capture — a pin keys on the source URL, not a frame).
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

  // Media URL extensions that mark a bare-URL target as a video (vs an image) — only used
  // to label detect()'s `kind` for a raw URL; element/entry targets carry their own kind.
  const VIDEO_FMTS = new Set(['mp4', 'webm', 'mov', 'm4v', 'mkv', 'avi', 'ogv', 'ogg']);

  // Inspect a target WITHOUT acting on it. Accepts a scanned entry, a stencil.items index,
  // a DOM element (e.g. a document.querySelector result), or an image/video URL — the same
  // union open()/pin() take, minus arrays. Returns a plain descriptor of what Stencil sees,
  // or null when the target carries nothing grabbable (a <div> with no background, a <video>
  // with no src/poster/frame, an out-of-range index, a non-target value). Never throws.
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
      // Does it currently appear in stencil.items (i.e. survive the live filters)?
      listed: entries().some((e) => (el && e.element === el) || (!!url && e.url === url)),
    };
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
    // The currently-pinned scanned entries (honors the live filters, like `items`).
    get pins() { return scanFiltered().filter((e) => e.pinned); },
    // The poster image of every <video> that declares one.
    get posters() { return scanPosters().filter(passes); },
    // Per-format on/off toggles: stencil.formats.png = false. Object.keys lists the
    // formats present on the page; the list getters honor the toggles.
    get formats() { return formatsToggle(); },
    // Per-category on/off toggles: stencil.kinds.video = false (image/background/video/poster).
    get kinds() { return kindsToggle(); },
    // ── Other filter controls (mirror the popup; the list getters above honor them) ──
    get searchText() { return filters.searchText; }, set searchText(v) { filters.searchText = String(v || ''); onFilterChange(); },
    // Treat searchText as a case-insensitive RegExp (mirrors the popup's regex checkbox).
    get regex() { return filters.regex; }, set regex(v) { filters.regex = !!v; onFilterChange(); },
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
      filters.searchText = ''; filters.regex = false; filters.disabledFormats.clear();
      filters.image = filters.background = filters.video = filters.poster = true;
      filters.minWidth = filters.maxWidth = filters.minHeight = filters.maxHeight = null;
      clearHighlight();
      persistFilters();
      return this;
    },
    // One-off query: scanned entries whose name or URL matches q (ignores the live filters).
    // opts.regex treats q as a case-insensitive RegExp (invalid → no matches); default is a
    // case-insensitive substring. Empty q returns every scanned entry.
    search(q, opts = {}) {
      const query = String(q || '');
      if (!query) return scan();
      return scan().filter((e) => searchMatch(`${e.name} ${e.url}`, query, !!opts.regex));
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
    // Open a target. opts: { incognito, newTab, desktop, poster, frame }. Default opens the
    // in-page editor modal; newTab opens the editor in a new browser tab; desktop hands the
    // bytes to the installed desktop app via its stencil:// scheme (like the popup's
    // "Open in… Desktop app") — needs a configured desktop scheme.
    open(target, opts = {}) {
      const r = resolveTarget(target, opts);
      send({ type: MSG.PAGE_OPEN, url: r.url, dataUrl: r.dataUrl, name: r.name, source: r.source, resource: location.href, incognito: !!opts.incognito, newTab: !!opts.newTab, desktop: !!opts.desktop });
      return this;
    },
    // Open a target in the quick-crop tool. opts: { album, poster }.
    crop(target, opts = {}) {
      const r = resolveTarget(target, opts);
      send({ type: MSG.PAGE_CROP, url: r.url, dataUrl: r.dataUrl, source: r.source, resource: location.href, album: !!opts.album });
      return this;
    },
    // Pin / unpin a target so it floats to the top of the popup list (and shows in the
    // options page's pinned viewer). Accepts an entry, a stencil.items index, an element,
    // a URL, or an array of those. Chainable.
    pin(target) { for (const t of resolvePinTargets(target)) setPinnedState(t, true); return this; },
    unpin(target) { for (const t of resolvePinTargets(target)) setPinnedState(t, false); return this; },
    // Inspect a target without acting on it → { kind, url, name, format, element, hasFrame,
    // hasPoster, pinned, isEdited, listed } or null. Accepts a scanned entry, a stencil.items
    // index, a DOM element (e.g. document.querySelector('img')), or an image/video URL.
    detect(target) { return describeTarget(target); },
    // True when Stencil can grab `target` (it has an image/video/background source, or a
    // capturable video frame) — i.e. it's a valid open()/crop()/pin() target. Never throws,
    // so it's the safe pre-check before pinning a querySelector result. Same accepted types
    // as detect(); for an array, test each item (`arr.every(stencil.grabbable)`).
    grabbable(target) { return !!describeTarget(target); },
  };

  // Hide every member from enumeration so the console shows a clean `stencil` (no
  // __stencil tag / method dump on console.log/Object.keys); access and DevTools
  // autocomplete still work (non-enumerable ≠ inaccessible). Must run before the freeze
  // below (freeze locks descriptors). __stencil stays a property (back-off guard reads
  // it) but no longer leaks into enumeration.
  for (const k of Reflect.ownKeys(api)) {
    const d = Object.getOwnPropertyDescriptor(api, k);
    if (d.enumerable) Object.defineProperty(api, k, { ...d, enumerable: false });
  }
  // Hard-guard with the same proxy as entries: writing a method, read-only getter, or the
  // internal __stencil tag THROWS (`stencil.open = 0` / `stencil.__stencil = 'x'`). Only
  // legit setter is `enabled`, which the trap delegates to. Methods `return this` (the
  // proxy when called on it), so chaining holds. guard() also does the Object.freeze.
  const guarded = guard(api);

  // Lock the binding against plain reassignment (writable:false). configurable:true kept
  // deliberately: on the editor's own page its (non-configurable) window.stencil must be
  // able to take over — and the back-off guard above already yields to it.
  try {
    Object.defineProperty(window, 'stencil', { value: guarded, writable: false, configurable: true, enumerable: false });
  } catch { /* a non-configurable window.stencil already exists (the editor) — leave it */ }

  // Ask the bridge to (re)push pins / edited / filters / highlight colour now that our
  // message listener is installed. The bridge pushes once at document_start — before this
  // MAIN-world script runs at document_idle — so without this request that state is missed
  // and getters fall back to defaults (e.g. the highlight stays the default colour).
  try { send({ type: MSG.PAGE_REQUEST_SYNC }); } catch { /* bridge not present */ }
})();
