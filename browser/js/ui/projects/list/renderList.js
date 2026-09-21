// Rebuilding the projects list, and holding it steady while rows fly out. The plan it paints is
// plan.js; what a wipe must outlive is here, so an out-of-band refresh cannot cut in.
import {
  wipeDurationMs, materialize, filterDelta, rowDustGrid, ROW_ARRIVE_MS, ROW_ARRIVE_DELAY_MS,
} from '../../motion.js';
import { showsRemoteSkeletons } from '../../../core/remote/listing.js';
import { makeSkeletonRow, emptyLabelFor } from '../row/placeholderRows.js';

export function createRenderList(ctx) {
  const { app, list, search, clearAllBtn, remotes, remoteObjectUrls, rowPlan, showsServer,
    filterMode, hasServers, ensureRemotes, keyMeta, attachRowDrag, hideZoom, closeMenu,
    selected, selectables, updateBatchBar } = ctx;
  // The "before" side of a filter transition — one array, so the caller's `keys()` sees it.
  const shownKeys = [];
  const rowByFilterKey = (key) =>
    list.querySelector(`[data-filter-key="${String(key).replace(/["\\]/g, '\\$&')}"]`);

  const render = () => {
    const q = search.value || '';
    hideZoom();
    closeMenu();   // a rebuilt list invalidates any open row menu
    selectables.clear();   // repopulated below by every row that wires a checkbox
    for (const u of remoteObjectUrls) URL.revokeObjectURL(u);
    remoteObjectUrls.clear();
    list.innerHTML = '';
    const showServer = showsServer();

    if (showServer && hasServers()) ensureRemotes();
    keyMeta.clear();
    shownKeys.length = 0;
    for (const entry of rowPlan()) {
      const row = entry.build();
      // On every row (the synthetic ones too), for the filter transition's delta.
      row.dataset.filterKey = entry.key;
      if (entry.item) { keyMeta.set(entry.key, entry.item); attachRowDrag(row, entry.key); }
      list.appendChild(row);
      shownKeys.push(entry.key);
    }

    // Only while a fetch is genuinely in flight (showsRemoteSkeletons).
    const loadingRemotes = showsRemoteSkeletons({
      showServer, hasServers: hasServers(), cache: remotes.cache, loading: remotes.loading,
    });
    if (loadingRemotes) { list.appendChild(makeSkeletonRow()); list.appendChild(makeSkeletonRow()); }

    if (!list.querySelector('.project-row:not(.project-skeleton)') && !loadingRemotes) {
      const empty = document.createElement('div');
      empty.className = 'info-empty';
      empty.textContent = remotes.failed ? 'Could not reach server.'
        : (q.trim() ? 'No matching projects.' : emptyLabelFor(filterMode()));
      list.appendChild(empty);
    }

    // "Clear All" only ever wipes local projects; with a server connected the label says so.
    const clearAllLabel = clearAllBtn.querySelector('span');
    if (clearAllLabel) clearAllLabel.textContent = hasServers() ? 'Clear All Local' : 'Clear All';
    clearAllBtn.dataset.title = hasServers()
      ? 'Delete every local project (server projects are not affected)'
      : 'Delete every saved project';
    // The synthetic "temporary (unsaved)" row is not a saved project: with just that on
    // screen, Clear All wiped nothing and the row came straight back.
    const clearable = list.querySelectorAll('.project-row:not(.project-temp):not(.project-remote):not(.project-skeleton)').length;
    clearAllBtn.disabled = clearable === 0;
    clearAllBtn.dataset.disabledReason = 'No saved projects to clear';

    // Drop selections whose project is gone (here or in another tab), against what the app
    // knows — never the filtered view, which must not drop a row from a pending batch.
    for (const [key, entry] of selected) {
      if (entry?.kind === 'local' && entry.id != null && !app.storage.store.getMeta(entry.id))
        selected.delete(key);
    }
    updateBatchBar();
  };

  // Call BEFORE a removal; await what it returns after. Only the held height and out-of-band
  // refreshes (canRefreshList) wait out wipeDurationMs (desktop twin: ProjectsDialog::retireRow).
  let removalsInFlight = 0;
  const beginRemoval = () => {
    removalsInFlight++;
    const held = list.getBoundingClientRect().height;
    if (held) list.style.minHeight = `${held}px`;
    const before = [...shownKeys];
    return async () => {
      // Let the leaving ash thin first: the row and its arrival land together, so it is
      // never seen plain and then veiled again, and its motes are not lost in the scatter.
      await new Promise((r) => setTimeout(r, ROW_ARRIVE_DELAY_MS));
      render();
      // Only rows the settle ADDED materialize, on the arrival clock (desktop: dustRowIn).
      const { entering } = filterDelta(before, shownKeys);
      entering.forEach((key, i) => {
        const el = rowByFilterKey(key);
        if (el) materialize(el, { ...rowDustGrid(entering.length, i), dustMs: ROW_ARRIVE_MS });
      });
      // The hold outlives the arrival, or a refresh re-renders the row out from under its motes.
      await new Promise((r) => setTimeout(r, Math.max(0, wipeDurationMs() - ROW_ARRIVE_DELAY_MS)));
      removalsInFlight = Math.max(0, removalsInFlight - 1);
      list.style.minHeight = '';
    };
  };

  return { render, beginRemoval, removing: () => removalsInFlight > 0, shownKeys, rowByFilterKey };
}
