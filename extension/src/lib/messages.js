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
  HL_HOVER: 'stencil-hl-hover',         // page highlight (highlight.js) → open panel: source URL now under the cursor ('' = none)
  REGISTRY: 'stencil-registry',         // editorBridge → SW: the editor's project registry
  EDITOR_SWITCH: 'stencil-editor-switch', // panel → editorBridge (on the editor tab): switch to the project for a source (resume without a new tab)
  OPEN_TAB: 'stencil-open-tab',         // overlay → SW: open a URL in a new tab
  DROPZONES_ARM: 'stencil-dropzones-arm',   // panel → SW: inject the on-page 4-quadrant drop overlay on `tabId` (a row drag started)
  DROPZONES_DISARM: 'stencil-dropzones-disarm', // panel → SW: remove the drop overlay on `tabId` (drag ended without a page drop)
  PAGE_DROP: 'stencil-page-drop',       // drop overlay (dropZones.js) → SW: a row was dropped in a quadrant → run its action
  PAGE_OPEN: 'stencil-page-open',       // page API → bridge → SW: open a target in the editor
  PAGE_CROP: 'stencil-page-crop',       // page API → bridge → SW: open a target in quick-crop
  PAGE_PIN: 'stencil-page-pin',         // page API → bridge → SW: pin / unpin a target
  PAGE_REQUEST_SYNC: 'stencil-page-request-sync', // page API → bridge: (re)push pins/edited/filters/hl-colour; NOT relayed to the SW
  PAGE_DISABLE: 'stencil-page-disable', // page API → bridge → SW: turn the page scripting API off
  PAGE_SET_FILTERS: 'stencil-page-set-filters', // page API → bridge: persist filter state (popupFilters); NOT relayed to the SW
};

// window.postMessage `source` tags (page ↔ in-page bridge / modal handshake).
export const SRC = {
  PAGE_API: 'stencil-page-api',         // pageApiMain (MAIN world) → pageApiBridge (ISOLATED)
  PAGE_FILTERS: 'stencil-page-filters', // pageApiBridge (ISOLATED) → pageApiMain: pushed popup filters
  PAGE_PINS: 'stencil-page-pins',       // pageApiBridge (ISOLATED) → pageApiMain: pinned source URLs for this site
  PAGE_EDITED: 'stencil-page-edited',   // pageApiBridge (ISOLATED) → pageApiMain: opened/edited source URLs (ledger)
  PAGE_HL_COLOR: 'stencil-page-hl-color', // pageApiBridge (ISOLATED) → pageApiMain: resolved highlight outline colour
  MODAL: 'stencil-modal',               // quick-crop frame → overlay host (ready/close handshake)
};
