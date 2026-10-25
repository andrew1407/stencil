// ISOLATED-world half of the page API: MAIN (content/pageApiMain.js) has no chrome.*, so this
// script shares the page's `window` message bus and relays its requests to the SW, fire-and-forget.
// orphan.js's claim stops a second inject and stands down a copy a reload orphaned.
pageBridge: {
  const { on } = window.__stencilOrphan?.('__stencilPageBridge', window) ?? {};
  if (!on) break pageBridge;
  // mirror of lib/messages.js (classic content script — can't import)
  const SRC = { PAGE_API: 'stencil-page-api', PAGE_FILTERS: 'stencil-page-filters', PAGE_PINS: 'stencil-page-pins', PAGE_EDITED: 'stencil-page-edited', PAGE_HL_COLOR: 'stencil-page-hl-color' };
  const MSG = { PAGE_SET_FILTERS: 'stencil-page-set-filters', PAGE_REQUEST_SYNC: 'stencil-page-request-sync' };
  // Runs on EVERY page, and `e.source === window` only proves same-document — so a WHITELIST:
  // these four are the page API's own surface, all scoped to this page.
  const RELAYABLE = new Set([
    'stencil-page-open', 'stencil-page-crop', 'stencil-page-pin', 'stencil-page-disable',
  ]);
  const FILTERS_KEY = 'popupFilters';   // must match popup.js — the page API ↔ popup shared filter state
  const PINS_KEY = 'stencil-pinned';    // must match lib/prefs/pins.js
  const LEDGER_KEY = 'stencil-opened';  // must match lib/prefs/ledger.js
  const ACCENT_KEY = 'stencil_accent';  // must match lib/accent/accent.js mirror
  // mirror of lib/highlight/color.js ACCENT_HEX — keep in sync.
  const ACCENT_HEX = { violet: '#7c3aed', burgundy: '#660033', pink: '#ec4899', crimson: '#be123c', maroon: '#550000', orange: '#ea580c', brown: '#a87c50', yellow: '#eab308', grass: '#16a34a', green: '#047857', turquoise: '#40e0d0', aqua: '#0891b2', sky: '#0ea5e9', bluegray: '#7394b3', grey: '#64748b', blue: '#2563eb' };
  const resolveHl = (setting, accentKey) => (!setting || setting === 'theme') ? (ACCENT_HEX[accentKey] || ACCENT_HEX.violet) : setting;

  // This page's origin — the "site" pins are grouped under (matches lib/prefs/pins.js siteOf).
  const site = (() => { try { return new URL(location.href).origin; } catch { return ''; } })();
  const srcOf = (e) => (e && typeof e.source === 'string' ? e.source.trim() : '');

  const pushFilters = (f) => window.postMessage({ source: SRC.PAGE_FILTERS, filters: f || null }, '*');
  const pushPins = (entries) => window.postMessage({
    source: SRC.PAGE_PINS,
    sources: (Array.isArray(entries) ? entries : []).filter((e) => e && e.site === site).map(srcOf).filter(Boolean),
  }, '*');
  // Only this site's ledger: the page must not learn what the user opened from other sites.
  const originOf = (u) => { try { return new URL(String(u || '')).origin; } catch { return ''; } };
  const pushEdited = (entries) => window.postMessage({
    source: SRC.PAGE_EDITED,
    sources: (Array.isArray(entries) ? entries : []).filter((e) => e && originOf(e.resource) === site).map(srcOf).filter(Boolean),
  }, '*');
  // A page's filter write is the popup's shape and nothing more: known keys, bounded values.
  const SEARCH_MAX = 500, FORMATS_MAX = 32, FORMAT_MAX = 16;
  const dim = (v) => (typeof v === 'number' && Number.isFinite(v) && v >= 0 ? v : null);
  const cleanFilters = (raw) => {
    const f = raw ?? {};   // nothing named is the defaults, as an empty write always was
    if (typeof f !== 'object' || Array.isArray(f)) return null;
    const out = { search: String(f.search ?? '').slice(0, SEARCH_MAX), regex: f.regex === true };
    for (const k of ['minW', 'maxW', 'minH', 'maxH']) out[k] = dim(f[k]);
    for (const k of ['includeImg', 'includeBg', 'includeVideo', 'includePosters', 'includeMeta']) out[k] = f[k] !== false;
    out.disabledFormats = (Array.isArray(f.disabledFormats) ? f.disabledFormats : [])
      .filter((x) => typeof x === 'string' && x.length <= FORMAT_MAX).slice(0, FORMATS_MAX);
    return out;
  };
  // The setting lives in storage.sync; the accent key in storage.local.
  const pushHlColor = (setting, accentKey) => window.postMessage({ source: SRC.PAGE_HL_COLOR, color: resolveHl(setting, accentKey) }, '*');
  const refreshHlColor = () => chrome.storage.sync.get({ highlightColor: 'theme' })
    .then((s) => chrome.storage.local.get(ACCENT_KEY).then((l) => pushHlColor(s.highlightColor, l[ACCENT_KEY]))).catch(() => {});
  // On load AND on PAGE_REQUEST_SYNC — the API installs late (document_idle) and would miss
  // a one-shot load-time push.
  const syncAll = () => {
    chrome.storage.local.get([FILTERS_KEY, PINS_KEY, LEDGER_KEY]).then((r) => {
      pushFilters(r[FILTERS_KEY]); pushPins(r[PINS_KEY]); pushEdited(r[LEDGER_KEY]);
    }).catch(() => {});
    refreshHlColor();
  };
  try {
    syncAll();
    chrome.storage.onChanged.addListener((changes, area) => {
      if (area === 'local') {
        if (changes[FILTERS_KEY]) pushFilters(changes[FILTERS_KEY].newValue || null);
        if (changes[PINS_KEY]) pushPins(changes[PINS_KEY].newValue || []);
        if (changes[LEDGER_KEY]) pushEdited(changes[LEDGER_KEY].newValue || []);
        if (changes[ACCENT_KEY]) refreshHlColor();
      } else if (area === 'sync' && changes.highlightColor) {
        refreshHlColor();
      }
    });
  } catch { /* no chrome.storage */ }

  on('message', (e) => {
    if (e.source !== window) return;
    const d = e.data;
    if (!d || d.source !== SRC.PAGE_API || !d.message) return;
    const m = d.message;
    if (m.type === MSG.PAGE_REQUEST_SYNC) { syncAll(); return; }
    // Filter writes go to shared storage (which feeds the popup), NOT the service worker.
    if (m.type === MSG.PAGE_SET_FILTERS) {
      const clean = cleanFilters(m.filters);
      if (clean) try { chrome.storage.local.set({ [FILTERS_KEY]: clean }); } catch { /* storage gone */ }
      return;
    }
    if (!RELAYABLE.has(m.type)) return;
    // A desktop hand-off launches an OS handler: only on the user's own click or key, never a page's say-so.
    if (m.type === 'stencil-page-open' && m.desktop && !navigator.userActivation?.isActive) return;
    try {
      chrome.runtime.sendMessage(m).catch(() => { /* SW asleep / context gone */ });
    } catch { /* extension context invalidated */ }
  });
}
