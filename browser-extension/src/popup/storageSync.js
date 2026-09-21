// Everything another open surface or the options page can change under this one.
import { LEDGER_KEY } from '../lib/prefs/ledger.js';
import { PINS_KEY } from '../lib/prefs/pins.js';
import { CONNECTIONS_KEY } from '../lib/connection/connections.js';
import { FILTERS_KEY } from '../lib/highlight/filterUi.js';
import { state } from './list/model.js';
import { annotateOpened, annotatePinned, loadOpenInSettings } from './list/scan.js';
import { filterUi, applyFilters } from './list/filters.js';
import { loadShared, startSharedPolling } from './pin/sharedPins.js';
import { runHoverHighlight, highlightListRowForSource } from './row/hoverHighlight.js';

// Re-annotate in place, so badges and outlines update without a re-scan.
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[LEDGER_KEY] && state.all.length) {
    annotateOpened().then(applyFilters);
  }
  if (area === 'local' && changes[PINS_KEY] && state.all.length) {
    annotatePinned().then(applyFilters);
  }
  if (area === 'local' && changes[CONNECTIONS_KEY]) {
    loadShared().then(() => {
      startSharedPolling();
      applyFilters();
    });
  }
  // The three panels persist their filters to the same key; acceptExternal skips the
  // echo of this surface's own write.
  if (area === 'local' && changes[FILTERS_KEY]) {
    if (filterUi.acceptExternal(changes[FILTERS_KEY].newValue || null)) applyFilters();
  }
  if (area === 'sync' && (changes.markOpened || changes.openedFirst) && state.all.length) {
    annotateOpened().then(applyFilters);
  }
  if (area === 'sync' && changes.showPinned && state.all.length) {
    annotatePinned().then(applyFilters);
  }
  // Menus are built lazily on click, so the refreshed cache needs no re-render.
  if (area === 'sync' && (changes.desktopScheme || changes.telegramBotUsername)) {
    loadOpenInSettings();
  }
  if (area === 'sync' && changes.hoverHighlight) {
    state.hoverHighlight = changes.hoverHighlight.newValue !== false;
    const hh = document.getElementById('f-hover-hl');
    if (hh) hh.checked = state.hoverHighlight;
    if (!state.hoverHighlight) { runHoverHighlight(''); highlightListRowForSource(''); }
  }
});
