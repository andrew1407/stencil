// ISOLATED-world half of the page API: MAIN (content/pageApiMain.js) has no chrome.*, so this
// script shares the page's `window` message bus and relays its requests to the SW, fire-and-forget.
if (!window.__stencilPageBridge) {
  window.__stencilPageBridge = true;
  // mirror of lib/messages.js (classic content script — can't import)
  const SRC = { PAGE_API: 'stencil-page-api', PAGE_FILTERS: 'stencil-page-filters', PAGE_PINS: 'stencil-page-pins', PAGE_EDITED: 'stencil-page-edited', PAGE_HL_COLOR: 'stencil-page-hl-color' };
  const MSG = { PAGE_SET_FILTERS: 'stencil-page-set-filters', PAGE_REQUEST_SYNC: 'stencil-page-request-sync' };
  // Runs on EVERY page, and `e.source === window` only proves same-document — so a WHITELIST:
  // these four are the page API's own surface, all scoped to this page.
  const RELAYABLE = new Set([
    'stencil-page-open', 'stencil-page-crop', 'stencil-page-pin', 'stencil-page-disable',
  ]);
  const FILTERS_KEY = 'popupFilters';   // must match popup.js — the page API ↔ popup shared filter state
  const PINS_KEY = 'stencil-pinned';    // must match lib/pins.js
  const LEDGER_KEY = 'stencil-opened';  // must match lib/ledger.js
  const ACCENT_KEY = 'stencil_accent';  // must match lib/accent.js mirror
  // mirror of lib/highlightColor.js ACCENT_HEX — keep in sync.
  const ACCENT_HEX = { violet: '#7c3aed', burgundy: '#660033', pink: '#ec4899', crimson: '#be123c', maroon: '#550000', orange: '#ea580c', brown: '#a87c50', yellow: '#eab308', grass: '#16a34a', green: '#047857', turquoise: '#40e0d0', aqua: '#0891b2', sky: '#0ea5e9', bluegray: '#7394b3', grey: '#64748b', blue: '#2563eb' };
  const resolveHl = (setting, accentKey) => (!setting || setting === 'theme') ? (ACCENT_HEX[accentKey] || ACCENT_HEX.violet) : setting;

  // This page's origin — the "site" pins are grouped under (matches lib/pins.js siteOf).
  const site = (() => { try { return new URL(location.href).origin; } catch { return ''; } })();
  const srcOf = (e) => (e && typeof e.source === 'string' ? e.source.trim() : '');

  const pushFilters = (f) => window.postMessage({ source: SRC.PAGE_FILTERS, filters: f || null }, '*');
  const pushPins = (entries) => window.postMessage({
    source: SRC.PAGE_PINS,
    sources: (Array.isArray(entries) ? entries : []).filter((e) => e && e.site === site).map(srcOf).filter(Boolean),
  }, '*');
  const pushEdited = (entries) => window.postMessage({
    source: SRC.PAGE_EDITED,
    sources: (Array.isArray(entries) ? entries : []).map(srcOf).filter(Boolean),
  }, '*');
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

  window.addEventListener('message', (e) => {
    if (e.source !== window) return;
    const d = e.data;
    if (!d || d.source !== SRC.PAGE_API || !d.message) return;
    const m = d.message;
    if (m.type === MSG.PAGE_REQUEST_SYNC) { syncAll(); return; }
    // Filter writes go to shared storage (which feeds the popup), NOT the service worker.
    if (m.type === MSG.PAGE_SET_FILTERS) {
      try { chrome.storage.local.set({ [FILTERS_KEY]: m.filters || {} }); } catch { /* storage gone */ }
      return;
    }
    if (!RELAYABLE.has(m.type)) return;
    try {
      chrome.runtime.sendMessage(m).catch(() => { /* SW asleep / context gone */ });
    } catch { /* extension context invalidated */ }
  });
}
