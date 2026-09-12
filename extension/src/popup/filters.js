import { passesFilters } from '../lib/filters.js';
import { sharedMatchesSearch } from '../lib/imageModel.js';
import { createFilterUi } from '../lib/filterUi.js';
import { listEl, countEl, clearStatus } from './panelDom.js';
import { state, isPinned, isOpened } from './model.js';
import { renderRow, filterTransition } from './row.js';

export const filterUi = createFilterUi({ doc: document, onChange: () => applyFilters() });

export let filters = {};
export const renderCount = () => {
  const total = state.all.length + state.shared.length;
  countEl.textContent = total ? `(${state.filtered.length}/${total})` : '';
};

export const applyFilters = () => {
  filters = filterUi.read();
  filterUi.save();
  state.filtered = state.all.filter(it => passesFilters(it, filters));
  // Pinned first, then opened; Array.sort is stable, so scan order holds within a group.
  const rank = (it) => (isPinned(it) ? 2 : 0) + (state.openedFirst && isOpened(it) ? 1 : 0);
  if (state.showPinned || state.openedFirst)
    state.filtered.sort((a, b) => rank(b) - rank(a));
  const showServerEl = document.getElementById('f-server-pins');
  const storeSel = document.getElementById('f-server-store');
  state.showServerPins = !showServerEl || showServerEl.checked;
  const store = (storeSel && !storeSel.hidden) ? storeSel.value : 'all';
  let sharedRows = state.shared.filter(s => sharedMatchesSearch(s, filters.search, filters.regex));
  if (!state.showServerPins) sharedRows = [];
  else if (store !== 'all') sharedRows = sharedRows.filter(s => s.serverUrl === store);
  state.filtered = state.filtered.concat(sharedRows);
  // The list is rebuilt wholesale; the transition diffs the outgoing render against the new set.
  filterTransition.begin();
  listEl.innerHTML = '';
  renderCount();
  if (!state.all.length && !sharedRows.length) {
    listEl.innerHTML = '<li class="empty">No images found on this page.</li>';
  } else if (!state.filtered.length) {
    listEl.innerHTML = '<li class="empty">No images match the filters.</li>';
  } else {
    clearStatus();
    state.filtered.forEach(renderRow);
  }
  filterTransition.end();
};
