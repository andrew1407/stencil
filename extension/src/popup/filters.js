// ── Reading the filter controls, ranking the rows, and rebuilding the list. ──
import { passesFilters } from '../lib/filters.js';
import { sharedMatchesSearch } from '../lib/imageModel.js';
import { createFilterUi } from '../lib/filterUi.js';
import { listEl, countEl, clearStatus } from './panelDom.js';
import { state, isPinned, isOpened } from './model.js';
import { renderRow, filterTransition } from './row.js';

// Format pills, reading the controls, and filter persistence live in lib/filterUi.js;
// onChange routes a pill toggle through the same applyFilters pass a click takes.
export const filterUi = createFilterUi({ doc: document, onChange: () => applyFilters() });

// ── Filtering ──
export let filters = {};
export const renderCount = () => {
  const total = state.all.length + state.shared.length;
  countEl.textContent = total ? `(${state.filtered.length}/${total})` : '';
};

export const applyFilters = () => {
  filters = filterUi.read();
  filterUi.save();                       // persist the current filter state on every change
  state.filtered = state.all.filter(it => passesFilters(it, filters));
  // Float pinned images (primary) then already-opened images (secondary) to the top.
  // Array.sort is stable, so images keep their scan order within each group, and each
  // key is a no-op when its toggle is off — preserving the page's natural order.
  const rank = (it) => (isPinned(it) ? 2 : 0) + (state.openedFirst && isOpened(it) ? 1 : 0);
  if (state.showPinned || state.openedFirst)
    state.filtered.sort((a, b) => rank(b) - rank(a));
  // Server-pins filter: the checkbox shows/hides server-stored items (the golden cue on
  // local pins, gated below in renderRow, + the shared rows here); the select narrows the
  // shared rows to one connected server.
  const showServerEl = document.getElementById('f-server-pins');
  const storeSel = document.getElementById('f-server-store');
  state.showServerPins = !showServerEl || showServerEl.checked;
  const store = (storeSel && !storeSel.hidden) ? storeSel.value : 'all';
  // Shared (server) pins list after the page's own images, newest-first.
  let sharedRows = state.shared.filter(s => sharedMatchesSearch(s, filters.search, filters.regex));
  if (!state.showServerPins) sharedRows = [];
  else if (store !== 'all') sharedRows = sharedRows.filter(s => s.serverUrl === store);
  state.filtered = state.filtered.concat(sharedRows);
  // Rows this pass drops — a narrowed search, a format pill off, an unpin with "show
  // pinned" off — fade out where they stood, and the ones it admits ramp in. The list is
  // rebuilt wholesale, so the transition diffs the outgoing render against the new set;
  // a filter fade is deliberately lighter than the destructive leave a delete plays.
  filterTransition.begin();
  listEl.innerHTML = '';
  renderCount();
  if (!state.all.length && !sharedRows.length) {
    listEl.innerHTML = '<li class="empty">No images found on this page.</li>';
  } else if (!state.filtered.length) {
    listEl.innerHTML = '<li class="empty">No images match the filters.</li>';
  } else {
    // `.status:empty` is display:none, which can't transition — so fade it out first,
    // then empty it (lib/animations/reveal.css .status-leaving).
    clearStatus();
    // Render every matching row; thumbnails + size measurement load lazily on scroll.
    state.filtered.forEach(renderRow);
  }
  filterTransition.end();
};
