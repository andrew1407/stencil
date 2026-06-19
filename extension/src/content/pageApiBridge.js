// ── Page-API bridge (ISOLATED world) ────────────────────────────────────────
// The page-global window.stencil lives in the MAIN world (content/pageApiMain.js) so
// its entries can hold live DOM elements — but MAIN has no chrome.* APIs. This ISOLATED
// script shares the page's `window` message bus with MAIN and relays the API's action
// requests to the SW (owner of chrome.tabs / chrome.scripting). Fire-and-forget:
// opening the editor / crop is one-way, no response plumbed back.
if (!window.__stencilPageBridge) {
  window.__stencilPageBridge = true;
  // mirror of lib/messages.js (classic content script — can't import)
  const SRC = { PAGE_API: 'stencil-page-api', PAGE_FILTERS: 'stencil-page-filters' };
  const MSG = { PAGE_SET_FILTERS: 'stencil-page-set-filters' };
  const FILTERS_KEY = 'popupFilters';   // must match popup.js — the page API ↔ popup shared filter state

  // Push the stored popup filters to the MAIN-world API so its window.stencil filter
  // state mirrors the popup, and keep it live as the popup (or another tab) changes them.
  const pushFilters = (f) => window.postMessage({ source: SRC.PAGE_FILTERS, filters: f || null }, '*');
  try {
    chrome.storage.local.get(FILTERS_KEY).then((r) => pushFilters(r[FILTERS_KEY])).catch(() => {});
    chrome.storage.onChanged.addListener((changes, area) => {
      if (area === 'local' && changes[FILTERS_KEY]) pushFilters(changes[FILTERS_KEY].newValue || null);
    });
  } catch { /* no chrome.storage */ }

  window.addEventListener('message', (e) => {
    // Only same-window messages tagged by our MAIN-world API.
    if (e.source !== window) return;
    const d = e.data;
    if (!d || d.source !== SRC.PAGE_API || !d.message) return;
    const m = d.message;
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
