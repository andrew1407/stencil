// ── Cross-context message contracts ─────────────────────────────────────────
// Every chrome.runtime message `type` and window.postMessage `source` tag, named once
// so both ends reference a constant, not a bare string (a typo silently drops the message).
// Module contexts (background SW, crop.js, lib/*) import from here; injected CLASSIC
// content scripts (src/content/*.js, no ES-module support) and the serialized
// executeScript({func}) overlay keep a MIRROR tagged "mirror of lib/messages.js"
// (same convention as lib/pageImages.js). Keep the mirrors in sync.
//
// Almost everything here is FIRE-AND-FORGET. The editor-mode group below (EDITOR_* /
// SOURCE_TABS / SCAN_TAB + the SRC.EXT_* channels) is the FIRST request/response traffic:
// those handlers `return true` and always answer `{ok:true,…}` / `{ok:false,error}`, never a
// rejection — and any leg waiting on the editor PAGE times out (1500 ms) rather than hang.

// chrome.runtime.sendMessage / onMessage `type` values.
export const MSG = {
  WAKE: 'stencil-wake',                 // ctxTarget → SW: wake the lazy worker so the menu exists
  CTX: 'stencil-ctx',                   // ctxTarget → SW: the right-click target it resolved
  HL_HOVER: 'stencil-hl-hover',         // page highlight (highlight.js) → open panel: source URL now under the cursor ('' = none)
  REGISTRY: 'stencil-registry',         // editorBridge → SW: the editor's project registry
  EDITOR_SWITCH: 'stencil-editor-switch', // panel → editorBridge (on the editor tab): switch to the project for a source (resume without a new tab)
  OPEN_TAB: 'stencil-open-tab',         // overlay → SW: open a URL in a new tab
  OPEN_OPTIONS: 'stencil-open-options', // devtools panel → SW: open the options page (runtime.openOptionsPage is absent in devtools contexts)
  DROPZONES_ARM: 'stencil-dropzones-arm',   // panel → SW: inject the on-page 4-quadrant drop overlay on `tabId` (a row drag started)
  DROPZONES_DISARM: 'stencil-dropzones-disarm', // panel → SW: remove the drop overlay on `tabId` (drag ended without a page drop)
  PAGE_DROP: 'stencil-page-drop',       // drop overlay (dropZones.js) → SW: a row was dropped in a quadrant → run its action
  PAGE_OPEN: 'stencil-page-open',       // page API → bridge → SW: open a target in the editor
  PAGE_CROP: 'stencil-page-crop',       // page API → bridge → SW: open a target in quick-crop
  PAGE_PIN: 'stencil-page-pin',         // page API → bridge → SW: pin / unpin a target
  PAGE_REQUEST_SYNC: 'stencil-page-request-sync', // page API → bridge: (re)push pins/edited/filters/hl-colour; NOT relayed to the SW
  PAGE_DISABLE: 'stencil-page-disable', // page API → bridge → SW: turn the page scripting API off
  PAGE_SET_FILTERS: 'stencil-page-set-filters', // page API → bridge: persist filter state (popupFilters); NOT relayed to the SW
  // Editor mode (request/response — see the header note). The two-hop ones keep ONE type
  // across both legs: a panel's runtime.sendMessage only ever reaches the SW, a
  // tabs.sendMessage only ever reaches a content script, so the legs can't be confused.
  EDITOR_LIST: 'stencil-editor-list',   // panel / editor page API → SW: every open editor tab + the state its bridge reports
  EDITOR_STATE: 'stencil-editor-state', // SW / panel → editorBridge (one editor tab): that tab's project + image state
  EDITOR_IMPORT: 'stencil-editor-import', // panel / editor page API → SW → editorBridge: import an image INTO that editor tab (no new tab)
  EDITOR_SWITCH_PROJECT: 'stencil-editor-switch-project', // panel / editor page API → SW → editorBridge: switch that tab to one of its own projects
  EDITOR_CROP: 'stencil-editor-crop', // SW → editorBridge: open that editor tab's own crop dialog (drop-zone crop on an editor page)
  EDITOR_FOCUS_TAB: 'stencil-editor-focus-tab', // panel / editor page API → SW: focus an editor tab and raise its window
  SOURCE_TABS: 'stencil-source-tabs',   // panel / editor page API → SW: the other open http(s) tabs an image can be pulled from
  SCAN_TAB: 'stencil-scan-tab',         // panel / editor page API → SW: scan one tab for images (the popup's scanner, on a tab you're not on)
};

// window.postMessage `source` tags (page ↔ in-page bridge / modal handshake).
export const SRC = {
  PAGE_API: 'stencil-page-api',         // pageApiMain (MAIN world) → pageApiBridge (ISOLATED)
  PAGE_FILTERS: 'stencil-page-filters', // pageApiBridge (ISOLATED) → pageApiMain: pushed popup filters
  PAGE_PINS: 'stencil-page-pins',       // pageApiBridge (ISOLATED) → pageApiMain: pinned source URLs for this site
  PAGE_EDITED: 'stencil-page-edited',   // pageApiBridge (ISOLATED) → pageApiMain: opened/edited source URLs (ledger)
  PAGE_HL_COLOR: 'stencil-page-hl-color', // pageApiBridge (ISOLATED) → pageApiMain: resolved highlight outline colour
  MODAL: 'stencil-modal',               // quick-crop frame → overlay host (ready/close handshake)
  // Editor mode's two request/response postMessage channels. Both are id-correlated (the
  // reply carries the request's `id`) and both nest their arguments under `payload` —
  // the envelope's own `source` tag would otherwise collide with a hand-off's `source`
  // (the image's provenance URL). Same-window only (`e.source !== window` → ignore).
  EXT_REQ: 'stencil-ext-req',           // editorBridge (ISOLATED) → editor page (browser/js/core/extensionBridge.js): state / import / switch
  EXT_RES: 'stencil-ext-res',           // editor page → editorBridge: the reply to one EXT_REQ
  EXT_API: 'stencil-ext-api',           // editorApiMain (MAIN world, stencil.extension) → editorBridge (ISOLATED): a facade call to relay
  EXT_API_RES: 'stencil-ext-api-res',   // editorBridge → editorApiMain: the reply to one EXT_API call
};
