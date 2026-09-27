// window.stencil, part 2 of 4 (see pageApiMedia.js): the frozen entries and the page scan, and
// the live filters the popup's controls share. Reads part 1 from the shared namespace object and
// adds its own for the parts after it.
(() => {
  const K = Object.getOwnPropertyDescriptor(window, '__stencilPageApiParts')?.value;
  if (!K) return;
  const { MSG, pinnedSources, editedSources, send, cssImageUrls, srcsetUrls, nameFromUrl, formatOf,
    CSS_IMG_PROPS, PSEUDOS, entryDims } = K;

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
    open(opts) { return K.api.open(this, opts); },
    crop(opts) { return K.api.crop(this, opts); },
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
  // Mirrors lib/highlight/filters.js matchesSearch; the query stays raw so regex metacharacters survive.
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

  Object.assign(K, { guard, setPinnedState, scan, filters, isNum, searchMatch, passes, scanFiltered, scanPosters });
})();
