import { highlightColorValue } from '../lib/highlightColor.js';
import { highlightSourceOnTab } from '../lib/hoverHighlight.js';
import { MSG } from '../lib/messages.js';
import { sameSource } from '../lib/dropEntry.js';
import { sourceOf, pinnable } from '../lib/imageModel.js';
import { state, rowElFor } from './model.js';

// ── Hover-to-highlight the page element ──────────────────────────────────────
// Hovering a row outlines its element on the page (lib/hoverHighlight.js). All row hovers
// share ONE debounced scheduler, so a sweep collapses to the last hovered source and a
// single injected call clears the old outline + sets the new one (no clear/set race).
const HOVER_HL_MS = 100;   // matches the preview debounce — the pointer must settle first
let hoverHlTimer = null;
let hoverHlPending = undefined;   // the last-requested source ('' = clear), or undefined = idle
let hoverHlPendingTab;            // …and the tab it belongs to (a merged scan spans several)
let hoverHlColor = null;          // resolved accent hex, cached across hovers
// `tabId` defaults to the first scanned page; a row from a MERGED editor-mode scan passes
// its own, so hovering it marks the page that image actually lives on.
export const runHoverHighlight = async (source, rowTabId) => {
  const tabId = rowTabId != null ? rowTabId : state.activeTabId;
  if (tabId == null) return;
  if (source && hoverHlColor == null) {
    try { hoverHlColor = await highlightColorValue(); } catch { hoverHlColor = '#7c3aed'; }
  }
  await highlightSourceOnTab(tabId, source, hoverHlColor);   // restricted page → false, ignored
};
const scheduleHoverHighlight = (source, rowTabId) => {
  if (!state.hoverHighlight) return;   // feature toggled off (highlight-on-hover checkbox)
  hoverHlPending = source;
  hoverHlPendingTab = rowTabId;
  clearTimeout(hoverHlTimer);
  hoverHlTimer = setTimeout(() => { runHoverHighlight(hoverHlPending, hoverHlPendingTab); }, HOVER_HL_MS);
};
// Bind a row to highlight its page element on hover — every surface, gated by the
// "highlight on hover" checkbox. Shared (server) rows point at a stored project, not a
// live page element, so they're skipped.
export const bindHoverHighlight = (rowEl, image) => {
  if (image.shared) return;
  const src = sourceOf(image);
  if (!src) return;
  rowEl.addEventListener('mouseenter', () => scheduleHoverHighlight(src, image.sourceTabId));
  rowEl.addEventListener('mouseleave', () => scheduleHoverHighlight('', image.sourceTabId));
};
// Clear the on-page outline when the surface goes away (side panel / DevTools panel
// persist, so the outline would otherwise linger on the page).
window.addEventListener('pagehide', () => { runHoverHighlight(''); });

// ── Reverse hover: outline the list row for the page element under the cursor ─────
// When the on-page highlight is active it reports the source under the cursor; outline the
// matching row and bring it into view. Only reacts to OUR target tab.
let listHlRow = null;
export const highlightListRowForSource = (source) => {
  if (listHlRow) { listHlRow.classList.remove('list-hl'); listHlRow = null; }
  if (!source) return;
  const image = state.filtered.find((im) => !im.shared && pinnable(im) && sameSource(sourceOf(im), source));
  if (!image) return;
  const row = rowElFor(image);
  if (!row) return;
  row.classList.add('list-hl');
  row.scrollIntoView({ block: 'nearest' });
  listHlRow = row;
};
chrome.runtime.onMessage.addListener((msg, sender) => {
  if (msg && msg.type === MSG.HL_HOVER && state.hoverHighlight && sender.tab && sender.tab.id === state.activeTabId) {
    highlightListRowForSource(msg.source || '');
  }
});
