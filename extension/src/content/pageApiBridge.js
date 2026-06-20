// ── Page-API bridge (ISOLATED world) ────────────────────────────────────────
// The page-global window.stencil lives in the MAIN world (content/pageApiMain.js) so
// its entries can hold live DOM elements — but MAIN has no chrome.* APIs. This ISOLATED
// script shares the page's `window` message bus with MAIN and relays the API's action
// requests to the SW (owner of chrome.tabs / chrome.scripting). Fire-and-forget:
// opening the editor / crop is one-way, no response plumbed back.
if (!window.__stencilPageBridge) {
  window.__stencilPageBridge = true;
  // mirror of lib/messages.js (classic content script — can't import)
  const SRC = { PAGE_API: 'stencil-page-api', PAGE_FILTERS: 'stencil-page-filters', PAGE_PINS: 'stencil-page-pins', PAGE_EDITED: 'stencil-page-edited', PAGE_HL_COLOR: 'stencil-page-hl-color' };
  const MSG = { PAGE_SET_FILTERS: 'stencil-page-set-filters', PAGE_REQUEST_SYNC: 'stencil-page-request-sync' };
  const FILTERS_KEY = 'popupFilters';   // must match popup.js — the page API ↔ popup shared filter state
  const PINS_KEY = 'stencil-pinned';    // must match lib/pins.js
  const LEDGER_KEY = 'stencil-opened';  // must match lib/ledger.js
  const ACCENT_KEY = 'stencil_accent';  // must match lib/accent.js mirror
  // mirror of lib/highlightColor.js ACCENT_HEX — keep in sync.
  const ACCENT_HEX = { violet: '#7c3aed', pink: '#ec4899', yellow: '#eab308', orange: '#ea580c', crimson: '#be123c', aqua: '#0891b2', sky: '#0ea5e9', blue: '#2563eb', grass: '#16a34a', green: '#047857', brown: '#a87c50', grey: '#64748b' };
  const resolveHl = (setting, accentKey) => (!setting || setting === 'theme') ? (ACCENT_HEX[accentKey] || ACCENT_HEX.violet) : setting;

  // This page's origin — the "site" pins are grouped under (matches lib/pins.js siteOf).
  const site = (() => { try { return new URL(location.href).origin; } catch { return ''; } })();
  const srcOf = (e) => (e && typeof e.source === 'string' ? e.source.trim() : '');

  // Push the stored popup filters to the MAIN-world API so its window.stencil filter
  // state mirrors the popup, and keep it live as the popup (or another tab) changes them.
  const pushFilters = (f) => window.postMessage({ source: SRC.PAGE_FILTERS, filters: f || null }, '*');
  // Push the source URLs pinned ON THIS SITE → entry.pinned reads them synchronously.
  const pushPins = (entries) => window.postMessage({
    source: SRC.PAGE_PINS,
    sources: (Array.isArray(entries) ? entries : []).filter((e) => e && e.site === site).map(srcOf).filter(Boolean),
  }, '*');
  // Push every opened/edited source URL (the ledger is keyed globally by source) →
  // entry.isEdited reads them synchronously.
  const pushEdited = (entries) => window.postMessage({
    source: SRC.PAGE_EDITED,
    sources: (Array.isArray(entries) ? entries : []).map(srcOf).filter(Boolean),
  }, '*');
  // Push the resolved highlight outline colour (accent or custom) → stencil.highlightOnPage
  // outlines match the theme. Setting lives in storage.sync; the accent key in storage.local.
  const pushHlColor = (setting, accentKey) => window.postMessage({ source: SRC.PAGE_HL_COLOR, color: resolveHl(setting, accentKey) }, '*');
  const refreshHlColor = () => chrome.storage.sync.get({ highlightColor: 'theme' })
    .then((s) => chrome.storage.local.get(ACCENT_KEY).then((l) => pushHlColor(s.highlightColor, l[ACCENT_KEY]))).catch(() => {});
  // Push every piece of state the MAIN-world API mirrors. Called on load AND whenever the
  // API asks (PAGE_REQUEST_SYNC) — the API installs late (document_idle) and would miss a
  // one-shot load-time push, so it requests this once it's listening.
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
    // Only same-window messages tagged by our MAIN-world API.
    if (e.source !== window) return;
    const d = e.data;
    if (!d || d.source !== SRC.PAGE_API || !d.message) return;
    const m = d.message;
    // The API asks for the current state once it's listening — push it (don't relay).
    if (m.type === MSG.PAGE_REQUEST_SYNC) { syncAll(); return; }
    // Filter writes go to shared storage (which feeds the popup), NOT the service worker.
    if (m.type === MSG.PAGE_SET_FILTERS) {
      try { chrome.storage.local.set({ [FILTERS_KEY]: m.filters || {} }); } catch { /* storage gone */ }
      return;
    }
    try {
      chrome.runtime.sendMessage(m).catch(() => { /* SW asleep / context gone */ });
    } catch { /* extension context invalidated */ }
  });
}
