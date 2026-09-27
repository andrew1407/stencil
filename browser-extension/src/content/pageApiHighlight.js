// window.stencil, part 3 of 4 (see pageApiMedia.js): the on-page highlight, the filter and pin
// state the bridge pushes, and the stencil.formats / stencil.kinds toggles. Reads parts 1–2 from
// the shared namespace object and adds its own for pageApiMain.
(() => {
  const K = Object.getOwnPropertyDescriptor(window, '__stencilPageApiParts')?.value;
  if (!K) return;
  const { MSG, SRC, pinnedSources, editedSources, send, scan, filters, scanFiltered } = K;
  let hlColor = '#7c3aed';

  // Shares the popup's <style id=stencil-hl-style> + data-stencil-hl attr, so toggling here is
  // detected by the popup and vice versa (lib/highlight/highlight.js).
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

  Object.assign(K, { highlightActive, applyHighlight, clearHighlight, persistFilters, onFilterChange, formatsToggle, kindsToggle });
})();
