import { highlightColorValue } from '../lib/highlight/highlightColor.js';
import { highlightSourceOnTab } from '../lib/highlight/hoverHighlight.js';
import { MSG } from '../lib/messages.js';
import { sameSource } from '../lib/drop/dropEntry.js';
import { sourceOf, pinnable } from '../lib/image/imageModel.js';
import { state, rowElFor } from './model.js';

// All row hovers share ONE debounced scheduler, so a sweep collapses to the last hovered
// source and a single injected call clears the old outline and sets the new (no race).
const HOVER_HL_MS = 100;   // matches the preview debounce
let hoverHlTimer = null;
let hoverHlPending = undefined;   // '' = clear, undefined = idle
let hoverHlPendingTab;
let hoverHlColor = null;
export const runHoverHighlight = async (source, rowTabId) => {
  const tabId = rowTabId != null ? rowTabId : state.activeTabId;
  if (tabId == null) return;
  if (source && hoverHlColor == null) {
    try { hoverHlColor = await highlightColorValue(); } catch { hoverHlColor = '#7c3aed'; }
  }
  await highlightSourceOnTab(tabId, source, hoverHlColor);
};
const scheduleHoverHighlight = (source, rowTabId) => {
  if (!state.hoverHighlight) return;
  hoverHlPending = source;
  hoverHlPendingTab = rowTabId;
  clearTimeout(hoverHlTimer);
  hoverHlTimer = setTimeout(() => { runHoverHighlight(hoverHlPending, hoverHlPendingTab); }, HOVER_HL_MS);
};
// Shared rows point at a stored project, not a live page element.
export const bindHoverHighlight = (rowEl, image) => {
  if (image.shared) return;
  const src = sourceOf(image);
  if (!src) return;
  rowEl.addEventListener('mouseenter', () => scheduleHoverHighlight(src, image.sourceTabId));
  rowEl.addEventListener('mouseleave', () => scheduleHoverHighlight('', image.sourceTabId));
};
// The outline would otherwise linger on the page after a docked panel closes.
window.addEventListener('pagehide', () => { runHoverHighlight(''); });

// Reverse hover: the on-page highlight reports the source under the cursor.
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
