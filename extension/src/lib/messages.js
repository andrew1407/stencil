// ── Cross-context message contracts ─────────────────────────────────────────
// Every chrome.runtime message `type` and window.postMessage `source` tag, named once
// so both ends reference a constant, not a bare string (a typo silently drops the message).
// Module contexts (background SW, crop.js, lib/*) import from here; injected CLASSIC
// content scripts (src/content/*.js, no ES-module support) and the serialized
// executeScript({func}) overlay keep a MIRROR tagged "mirror of lib/messages.js"
// (same convention as lib/pageImages.js). Keep the mirrors in sync.

// chrome.runtime.sendMessage / onMessage `type` values.
export const MSG = {
  WAKE: 'stencil-wake',                 // ctxTarget → SW: wake the lazy worker so the menu exists
  CTX: 'stencil-ctx',                   // ctxTarget → SW: the right-click target it resolved
  REGISTRY: 'stencil-registry',         // editorBridge → SW: the editor's project registry
  OPEN_TAB: 'stencil-open-tab',         // overlay → SW: open a URL in a new tab
  PAGE_OPEN: 'stencil-page-open',       // page API → bridge → SW: open a target in the editor
  PAGE_CROP: 'stencil-page-crop',       // page API → bridge → SW: open a target in quick-crop
  PAGE_DISABLE: 'stencil-page-disable', // page API → bridge → SW: turn the page scripting API off
  PAGE_SET_FILTERS: 'stencil-page-set-filters', // page API → bridge: persist filter state (popupFilters); NOT relayed to the SW
};

// window.postMessage `source` tags (page ↔ in-page bridge / modal handshake).
export const SRC = {
  PAGE_API: 'stencil-page-api',         // pageApiMain (MAIN world) → pageApiBridge (ISOLATED)
  PAGE_FILTERS: 'stencil-page-filters', // pageApiBridge (ISOLATED) → pageApiMain: pushed popup filters
  MODAL: 'stencil-modal',               // quick-crop frame → overlay host (ready/close handshake)
};
