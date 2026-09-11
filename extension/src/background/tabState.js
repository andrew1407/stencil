// ── Per-tab right-click state + the pins snapshot ───────────────────────────
// What the ctxTarget probe last resolved, per tab, plus the pins cache the probe reads
// synchronously to relabel Pin ↔ Unpin before the native menu appears.
import { loadPins, PINS_KEY } from '../lib/pins.js';

// What the probe last resolved under the cursor, per tab (ready { url } for a
// background or captured frame). Needed because info.srcUrl is absent (backgrounds)
// or wrong (a <video>'s media file, not a frame).
export const lastTargetByTab = new Map();
// Last right-click <video> target, per tab: { frameId, point, rect, dpr }. Lets the
// click handler recapture in-page and screenshot-crop the right rect for tainted media.
export const lastVideoByTab = new Map();
// Poster URL of the last right-clicked <video>, per tab — drives the Preview submenu.
export const lastPosterByTab = new Map();

// In-memory snapshot of the pinned store, kept fresh from storage. The context-menu
// probe relabels the Pin item (Pin ↔ Unpin) on right-click; that must be SYNCHRONOUS to
// beat the native menu appearing, so it reads this cache instead of awaiting loadPins().
export let pinsCache = [];
const refreshPinsCache = async () => { try { pinsCache = await loadPins(); } catch { /* keep the last snapshot */ } };
refreshPinsCache();
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[PINS_KEY]) pinsCache = Array.isArray(changes[PINS_KEY].newValue) ? changes[PINS_KEY].newValue : [];
});
chrome.tabs.onRemoved.addListener((tabId) => {
  lastTargetByTab.delete(tabId);
  lastVideoByTab.delete(tabId);
  lastPosterByTab.delete(tabId);
});
