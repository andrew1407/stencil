// window.stencil in every page's MAIN world, only while options → "Page scripting API" is on — part
// 4 of 4 (see pageApiMedia.js): the facade itself. MAIN world so entries carry live DOM elements;
// without chrome.* there, action requests are postMessage'd to the ISOLATED bridge
// (content/pageApiBridge.js).
(() => {
  const NS = '__stencilPageApiParts';
  const K = Object.getOwnPropertyDescriptor(window, NS)?.value;
  if (!K) return;
  try { delete window[NS]; } catch { /* the facade still stands on K */ }
  const {
    MSG, pinnedSources, editedSources, send, nameFromUrl, videoHasFrame, normFmt, formatOf, captureVideoFrame,
    elementUrl, guard, setPinnedState, scan, filters, isNum, searchMatch, passes, scanFiltered, scanPosters,
    highlightActive, applyHighlight, clearHighlight, persistFilters, onFilterChange, formatsToggle, kindsToggle,
  } = K;

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
  K.api = api;

  // Non-enumerable so the console shows a clean `stencil`. Must run before guard()'s freeze
  // locks the descriptors; __stencil stays a property (the back-off guard reads it).
  for (const k of Reflect.ownKeys(api)) {
    const d = Object.getOwnPropertyDescriptor(api, k);
    if (d.enumerable) Object.defineProperty(api, k, { ...d, enumerable: false });
  }
  // The only legit setter is `enabled`; methods `return this` so chaining holds. guard() freezes.
  const guarded = guard(api);

  // configurable:true on purpose: on the editor's own page its non-configurable window.stencil
  // must be able to take over (the back-off guard in pageApiMedia.js already yields to it).
  try {
    Object.defineProperty(window, 'stencil', { value: guarded, writable: false, configurable: true, enumerable: false });
  } catch { /* a non-configurable window.stencil already exists (the editor) — leave it */ }

  // The bridge pushed once at document_start, before this script ran at document_idle — ask
  // for pins / edited / filters / highlight colour again now that the listener exists.
  try { send({ type: MSG.PAGE_REQUEST_SYNC }); } catch { /* bridge not present */ }
})();
