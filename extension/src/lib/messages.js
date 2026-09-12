// Every chrome.runtime message `type` and window.postMessage `source` tag. Classic
// content scripts and the executeScript({func}) overlay cannot import, so they keep a
// mirror tagged "mirror of lib/messages.js" — tests/messages.test.js pins each one.

// Fire-and-forget, except the editor-mode group: those handlers `return true`, always
// answer `{ok, …}` (never a rejection), and time out (1500 ms) rather than hang on the page.
export const MSG = Object.freeze({
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
  // Two-hop types keep ONE name across both legs: runtime.sendMessage only reaches the
  // SW, tabs.sendMessage only a content script, so the legs cannot be confused.
  EDITOR_LIST: 'stencil-editor-list',   // panel / editor page API → SW: every open editor tab + the state its bridge reports
  EDITOR_STATE: 'stencil-editor-state', // SW / panel → editorBridge (one editor tab): that tab's project + image state
  EDITOR_IMPORT: 'stencil-editor-import', // panel / editor page API → SW → editorBridge: import an image INTO that editor tab (no new tab)
  EDITOR_SWITCH_PROJECT: 'stencil-editor-switch-project', // panel / editor page API → SW → editorBridge: switch that tab to one of its own projects
  EDITOR_CROP: 'stencil-editor-crop', // SW → editorBridge: open that editor tab's own crop dialog (drop-zone crop on an editor page)
  EDITOR_FOCUS_TAB: 'stencil-editor-focus-tab', // panel / editor page API → SW: focus an editor tab and raise its window
  SOURCE_TABS: 'stencil-source-tabs',   // panel / editor page API → SW: the other open http(s) tabs an image can be pulled from
  SCAN_TAB: 'stencil-scan-tab',         // panel / editor page API → SW: scan one tab for images (the popup's scanner, on a tab you're not on)
});

export const SRC = Object.freeze({
  PAGE_API: 'stencil-page-api',         // pageApiMain (MAIN world) → pageApiBridge (ISOLATED)
  PAGE_FILTERS: 'stencil-page-filters', // pageApiBridge (ISOLATED) → pageApiMain: pushed popup filters
  PAGE_PINS: 'stencil-page-pins',       // pageApiBridge (ISOLATED) → pageApiMain: pinned source URLs for this site
  PAGE_EDITED: 'stencil-page-edited',   // pageApiBridge (ISOLATED) → pageApiMain: opened/edited source URLs (ledger)
  PAGE_HL_COLOR: 'stencil-page-hl-color', // pageApiBridge (ISOLATED) → pageApiMain: resolved highlight outline colour
  MODAL: 'stencil-modal',               // quick-crop frame → overlay host (ready/close handshake)
  // Id-correlated request/response; arguments nest under `payload` so the envelope's
  // `source` tag cannot collide with a hand-off's `source` URL. Same-window only.
  EXT_REQ: 'stencil-ext-req',           // editorBridge (ISOLATED) → editor page (browser/js/core/extensionBridge.js): state / import / switch
  EXT_RES: 'stencil-ext-res',           // editor page → editorBridge: the reply to one EXT_REQ
  EXT_API: 'stencil-ext-api',           // editorApiMain (MAIN world, stencil.extension) → editorBridge (ISOLATED): a facade call to relay
  EXT_API_RES: 'stencil-ext-api-res',   // editorBridge → editorApiMain: the reply to one EXT_API call
});
