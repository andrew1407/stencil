// ── Everything another open surface (or the options page) can change under us. ──
import { LEDGER_KEY } from '../lib/ledger.js';
import { PINS_KEY } from '../lib/pins.js';
import { CONNECTIONS_KEY } from '../lib/connections.js';
import { FILTERS_KEY } from '../lib/filterUi.js';
import { state } from './model.js';
import { annotateOpened, annotatePinned, loadOpenInSettings } from './scan.js';
import { filterUi, applyFilters } from './filters.js';
import { loadShared, startSharedPolling } from './sharedPins.js';
import { runHoverHighlight, highlightListRowForSource } from './hoverHighlight.js';

// The ledger can change while a surface is open — notably a prune when a project is
// deleted in the editor (background.js → pruneLedger). Re-annotate scanned images in
// place so badges drop without a re-scan. Mainly serves the side panel / DevTools panel.
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[LEDGER_KEY] && state.all.length) {
    annotateOpened().then(applyFilters);
  }
  // Pins changed elsewhere (this surface, another open surface, or the page API) —
  // re-annotate in place so the gray outline / float updates without a re-scan.
  if (area === 'local' && changes[PINS_KEY] && state.all.length) {
    annotatePinned().then(applyFilters);
  }
  // Connections added/removed (Options page, or another surface) — re-pull shared pins
  // and (re)start polling so the golden-outlined rows appear/disappear without a rescan.
  if (area === 'local' && changes[CONNECTIONS_KEY]) {
    loadShared().then(() => {
      startSharedPolling();
      applyFilters();
    });
  }
  // Keep concurrently-open surfaces in lockstep: the popup, side panel, and DevTools
  // panel all run this controller and persist their filter state to the same key, so a
  // change in one should mirror into the others. Skip the echo of our own write.
  if (area === 'local' && changes[FILTERS_KEY]) {
    // Skips the echo of our own write; otherwise re-syncs the controls + the list.
    if (filterUi.acceptExternal(changes[FILTERS_KEY].newValue || null)) applyFilters();
  }
  // The opened-images settings (markOpened / openedFirst) live in storage.sync and are
  // also editable from the options page — reflect external changes here too.
  if (area === 'sync' && (changes.markOpened || changes.openedFirst) && state.all.length) {
    annotateOpened().then(applyFilters);   // annotateOpened re-syncs the two checkboxes
  }
  // The show-pinned setting also lives in storage.sync and is editable from options.
  if (area === 'sync' && changes.showPinned && state.all.length) {
    annotatePinned().then(applyFilters);   // annotatePinned re-syncs its checkbox
  }
  // The "Open in…" targets (desktop scheme / Telegram bot username) are edited on the
  // options page — refresh the cache so the ⋯ menu's Open-in items gate correctly without
  // a rescan (menus are built lazily on click, so no re-render is needed).
  if (area === 'sync' && (changes.desktopScheme || changes.telegramBotUsername)) {
    loadOpenInSettings();
  }
  // The highlight-on-hover setting changed in another open surface — mirror it here
  // (no re-annotate needed: it only gates the hover behaviour, not the list contents).
  if (area === 'sync' && changes.hoverHighlight) {
    state.hoverHighlight = changes.hoverHighlight.newValue !== false;
    const hh = document.getElementById('f-hover-hl');
    if (hh) hh.checked = state.hoverHighlight;
    if (!state.hoverHighlight) { runHoverHighlight(''); highlightListRowForSource(''); }
  }
});
