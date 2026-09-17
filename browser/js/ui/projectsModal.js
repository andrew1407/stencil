import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches, escapeHtml } from './base.js';
import { wireNameEditor, notify, isTouchLike, shortName, anchorPickerInput } from '../utils.js';
import { icon, setSelectAllFace } from './icons.js';
import { SORT_MODES, sortProjectItems } from './projectSort.js';
import {
  observeReveal, leaveThenRemove, wipeDurationMs, createFilterAnimator,
  materialize, filterDelta, rowDustGrid,
  ITEM_DUST_MS, rowLeaveDust, revealControls, revealBar,
  SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS, markIn, markOut,
} from './motion.js';
import { normalizeHex } from '../core/accents.js';
import { subscribe, EVENTS } from '../eventBus/appBus.js';

import { DOUBLE_CLICK_MS, DRAG_SLOP_PX, rowOpenIntent, createOpenGesture, canRefreshList } from '../core/projectOpenGesture.js';
import { createRemoteListing, showsRemoteSkeletons } from '../core/remoteListing.js';
import { createProjectRowMenu } from './projectRowMenu.js';
import { createThumbZoom } from './projectThumbZoom.js';
import { projectsModalInner } from './projects/markup.js';
import { createRemoteRow } from './projects/remoteRow.js';
import { attachRowActions, attachIncognitoActions } from './projects/rowActions.js';
import { createDragReorder } from './projects/dragReorder.js';
import { wireBatchActions } from './projects/batchActions.js';
import { createProjectSelection } from './projects/selection.js';
import { createLocalRow } from './projects/localRow.js';

// The projects chooser / switcher. Rows are built at runtime (the static #projects-list
// stays comment-only) to keep the markup tests green.
export class StencilProjectsModal extends StencilElement {
  static inner() { return projectsModalInner(); }
  static template() { return hostTag('stencil-projects-modal', 'id="projects-modal-overlay" class="app-modal-overlay"', StencilProjectsModal.inner()); }

  wire(app) {
    const overlay = document.getElementById('projects-modal-overlay');
    const openBtn = document.getElementById('projects-btn');
    const closeBtn = document.getElementById('projects-close');
    const search = document.getElementById('projects-search');
    const list = document.getElementById('projects-list');
    const newEditorBtn = document.getElementById('projects-new-editor');
    const clearAllBtn = document.getElementById('projects-clear-all');
    const filterEl = document.getElementById('projects-filter');
    const sortEl = document.getElementById('projects-sort');
    const searchModeEl = document.getElementById('projects-search-mode');
    const batchBar = document.getElementById('projects-batch-bar');
    const batchCount = document.getElementById('projects-batch-count');
    const store = app.storage.store;

    let peers = [];
    let incognitoPeers = [];
    // The worker echoes every tab's active id, ours included; shared by the row badge and the
    // "Open elsewhere" filter so the two never disagree.
    const isPeerOpen = (m) => peers.includes(m.id) && m.id !== app.storage.activeId;
    let filterMode = 'all';
    const hasServers = () => !!app.connections?.urls?.length;

    // Sort mode + manual drag order persist in sessionStorage (never the shared localStorage
    // registry, which has a C++ core twin under the parity contract).
    const SORT_KEY = 'stencil_projects_sortmode';
    const ORDER_KEY = 'stencil_projects_order';
    const SEARCH_MODE_KEY = 'stencil_projects_searchmode';
    const SEARCH_MODES = ['common', 'names', 'keywords'];
    const ssGet = (k) => { try { return window.sessionStorage.getItem(k); } catch { return null; } };
    const ssSet = (k, v) => { try { window.sessionStorage.setItem(k, v); } catch { /* private mode / disabled */ } };
    const loadSortMode = () => { const v = ssGet(SORT_KEY); return SORT_MODES.includes(v) ? v : 'name'; };
    const loadSearchMode = () => { const v = ssGet(SEARCH_MODE_KEY); return SEARCH_MODES.includes(v) ? v : 'common'; };
    let searchMode = loadSearchMode();
    const matchRow = (name, keywords, q) => {
      if (!q.trim()) return true;
      const kw = (keywords || []).join(' ');
      if (searchMode === 'names') return rowMatches(name || '', q);
      if (searchMode === 'keywords') return rowMatches(kw, q);
      return rowMatches(name || '', q) || rowMatches(kw, q);
    };
    const loadOrder = () => { try { const a = JSON.parse(ssGet(ORDER_KEY) || '[]'); return Array.isArray(a) ? a : []; } catch { return []; } };
    const saveOrder = (a) => ssSet(ORDER_KEY, JSON.stringify(a));
    let sortMode = loadSortMode();

    // One cached server listing per render cycle, so mixed/date sorts interleave from one
    // snapshot; invalidated on connection/server-project changes and on each open.
    const remotes = createRemoteListing(() => app.connections.remoteProjects());
    // Kept alive (not revoked on load) so the hover-magnify zoom can reuse them.
    const remoteObjectUrls = new Set();

    // Multi-select + the batch bar (ui/projects/selection.js).
    const {
      selected, doomed, selectables, batchBtns, sel, retireKey, localKey, remoteKey,
      isServerMeta, anyLiveSelectable, allSelected, updateBatchBar, updateSelectAll,
      clearSelection, toggleSelect,
    } = createProjectSelection({ batchBar, batchCount, hasServers: () => hasServers() });

    // Pick a connected server (auto when only one); null when cancelled. `closeAnchor` is
    // where the dialog's dust pours back into (a row's "⋯", since the menu row is gone by then).
    const pickServer = async (message, closeAnchor = null) => {
      const urls = app.connections?.urls || [];
      if (!urls.length) return null;
      if (urls.length === 1) return urls[0];
      return app.choose(message, { title: 'Choose server', confirmLabel: 'OK', confirmIcon: 'server', closeAnchor, options: urls.map(u => ({ value: u, label: u })) });
    };

    const fmtDate = ts => {
      if (!ts) return '';
      try {
        return new Date(ts).toLocaleString();
      } catch {
        return '';
      }
    };

    // The picture's size, its orientation and the description. No drawn-line length.
    const projectTooltip = meta => {
      const lines = [];
      const w = meta.imageW;
      const h = meta.imageH;
      if (w && h) lines.push(`${w}x${h} px · ${h >= w ? 'portrait' : 'landscape'}`);
      if (meta.description) lines.push(`Description: ${meta.description}`);
      return lines.join('\n');
    };

    const expiryLabel = meta => {
      if (store.isExpired(meta)) return { text: 'EXPIRED', expired: true, soon: false };
      const at = store.expiresAt(meta);
      if (at == null) return { text: '', expired: false, soon: false };
      const days = Math.max(0, Math.ceil((at - Date.now()) / (24 * 60 * 60 * 1000)));
      return {
        text: days <= 1 ? 'expires in 1 day' : `expires in ${days} days`,
        expired: false,
        soon: store.isExpiringSoon(meta),
      };
    };

    const { enableThumbZoom, hideZoom } = createThumbZoom();

    const { showMenu, closeMenu } = createProjectRowMenu();

    // A delete plays the row out before the rebuild.
    const rowById = (id) => (id == null ? null : list.querySelector(`[data-id="${id}"]`));

    // Call BEFORE a removal; await what it returns after. leaveThenRemove resolves on the
    // short collapse while the ash falls longer, so the list height, the re-render and
    // out-of-band refreshes (canRefreshList) are all held until wipeDurationMs.
    let removalsInFlight = 0;
    const beginRemoval = () => {
      removalsInFlight++;
      const held = list.getBoundingClientRect().height;
      if (held) list.style.minHeight = `${held}px`;
      const before = [...shownKeys];
      return async () => {
        await new Promise((r) => setTimeout(r, wipeDurationMs()));
        removalsInFlight = Math.max(0, removalsInFlight - 1);
        render();
        list.style.minHeight = '';
        // What the removal revealed (the pinned "Temporary (unsaved)" row) materializes
        // behind its own motes — only rows the settle ADDED. Desktop twin:
        // ListFilterFade::dustRowIn over a ProjectsDialog::refresh rebuild.
        const { entering } = filterDelta(before, shownKeys);
        entering.forEach((key, i) => {
          const el = rowByFilterKey(key);
          if (el) materialize(el, rowDustGrid(entering.length, i));
        });
      };
    };

    const scrollRowIntoView = (id) => {
      if (id == null) return;
      requestAnimationFrame(() => {
        const el = list.querySelector(`[data-id="${id}"]`);
        if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest' });
      });
    };

    // Opening replaces this tab's unsaved session (or spawns a tab), so it confirms first.
    const confirmOpen = (name, newTab = false, closeAnchor = null) => app.confirm(
      newTab
        ? `Open "${shortName(name || 'Untitled')}" in a new tab?`
        : `Open "${shortName(name || 'Untitled')}" here? Any unsaved changes in the current tab will be replaced.`,
      { title: 'Open project', confirmLabel: 'Open', confirmIcon: 'folder', cancelLabel: 'Cancel', closeAnchor });

    // One hidden colour field for the whole list. Never `display: none` and never built in
    // the click handler: the native picker opens beside its input's laid-out box
    // (utils.anchorPickerInput puts it under the pressed button).
    const colorInput = document.createElement('input');
    colorInput.type = 'color';
    colorInput.className = 'project-color-picker';
    colorInput.tabIndex = -1;
    colorInput.setAttribute('aria-hidden', 'true');
    let colorTarget = null;
    colorInput.addEventListener('change', () => {
      if (!colorTarget) return;
      app.setProjectColor(colorTarget.id, colorInput.value);
      colorTarget.color = colorInput.value;
      render();
    });
    // Beside the list: render() wipes the list's own innerHTML.
    (list.parentElement || list).appendChild(colorInput);
    const openColorPicker = (meta, btn) => {
      colorTarget = meta;
      colorInput.value = normalizeHex(meta.color) || '#7c3aed';
      anchorPickerInput(colorInput, btn);
      try {
        if (typeof colorInput.showPicker === 'function') colorInput.showPicker();
        else colorInput.click();
      } catch { colorInput.click(); }
    };

    // One local project row (ui/projects/localRow.js); `render` and `close` cross as thunks.
    const makeRow = createLocalRow({
      app, close: () => close(), render: () => render(),
      localKey, selected, selectables, isServerMeta, toggleSelect,
      enableThumbZoom, projectTooltip, fmtDate, expiryLabel, isPeerOpen, hasServers,
      pickServer, confirmOpen, scrollRowIntoView, openColorPicker, beginRemoval, retireKey,
      rowById, showMenu,
    });

    // One server-project row (ui/projects/remoteRow.js); `render`, `close`, `openRemote` and
    // `invalidateRemotes` are declared below, so they cross as thunks.
    const makeRemoteRow = createRemoteRow({
      app, close: () => close(), render: () => render(),
      remoteKey, selected, selectables, toggleSelect,
      enableThumbZoom, showMenu, remoteObjectUrls,
      projectTooltip, fmtDate, confirmOpen, openRemote: (m) => openRemote(m),
      scrollRowIntoView, beginRemoval, retireKey, rowById,
      invalidateRemotes: () => invalidateRemotes(),
    });

    // Shared with the external-launch server hand-off (DrawingApp.openRemoteProject).
    const openRemote = (meta) => app.openRemoteProject(meta);

    // Shown while the server listing loads, so the modal opens instantly.
    const makeSkeletonRow = () => {
      const row = document.createElement('div');
      row.className = 'project-row project-skeleton';
      row.innerHTML = '<div class="project-thumb skel"></div>'
        + '<div class="project-info"><div class="skel skel-line"></div>'
        + '<div class="skel skel-line short"></div></div>';
      return row;
    };

    // A skipped render is picked up by the settle render / endDrag's render instead.
    const mayRefresh = () => canRefreshList({
      open: overlay.classList.contains('modal-open'),
      dragging: isDragging(),
      removing: removalsInFlight > 0,
    });

    // ensureRemotes() fills the cache once, then re-renders (deferred while a wipe or drag
    // holds). Stale-fetch dropping lives in the factory, where it is unit-tested.
    const showsServer = () => filterMode === 'all' || filterMode === 'server';
    const ensureRemotes = () => {
      if (!showsServer() || !hasServers()) return;
      remotes.ensure(() => { if (mayRefresh()) render(); });
    };
    const invalidateRemotes = () => remotes.invalidate();

    // One flat, sortable item list: a stable key, a lowercased name + date for the
    // comparators, an isRemote flag, and a build() returning the row element.
    const localRowKey = (m) => `local:${m.id}`;
    const remoteRowKey = (m) => `remote:${m.serverUrl}:${m.id}`;
    const metaName = (m) => (m.name || '').toLowerCase();
    const metaDate = (m) => m.updatedAt || m.createdAt || 0;
    // "Open elsewhere" / "Not open elsewhere" are local-only scopes: a not-yet-claimed
    // remote-cache row has no peers relationship to filter on.
    const showsPeerOpen = () => filterMode === 'peer-open' || filterMode === 'peer-closed';
    const buildItems = ({ applySearch }) => {
      const q = applySearch ? (search.value || '') : '';
      const showLocal = filterMode === 'all' || filterMode === 'local' || showsPeerOpen();
      const showServer = showsServer();
      const items = [];
      const all = store.list()
        .filter((m) => !applySearch || matchRow(m.name, m.keywords, q))
        .filter((m) => filterMode !== 'peer-open' || isPeerOpen(m))
        .filter((m) => filterMode !== 'peer-closed' || !isPeerOpen(m));
      const localLinked = all.filter((m) => isServerMeta(m));
      if (showLocal) for (const meta of all.filter((m) => !isServerMeta(m)))
        items.push({ key: localRowKey(meta), name: metaName(meta), date: metaDate(meta), isRemote: false, meta, build: () => makeRow(meta) });
      if (showServer) for (const meta of localLinked)
        items.push({ key: localRowKey(meta), name: metaName(meta), date: metaDate(meta), isRemote: false, meta, build: () => makeRow(meta) });
      // Deduped against server-linked local rows.
      if (showServer && Array.isArray(remotes.cache)) {
        const claimed = new Set(localLinked.map((m) => `${m.address}|${m.remoteId}`));
        for (const meta of remotes.cache) {
          if (claimed.has(`${meta.serverUrl}|${meta.id}`)) continue;
          if (applySearch && !matchRow(meta.name, meta.keywords, q)) continue;
          items.push({ key: remoteRowKey(meta), name: metaName(meta), date: metaDate(meta), isRemote: true, meta, build: () => makeRemoteRow(meta) });
        }
      }
      return items;
    };
    const sortItems = (items, mode) => sortProjectItems(items, mode, loadOrder());
    const setSortMode = (m) => { sortMode = m; ssSet(SORT_KEY, m); if (sortEl) sortEl.value = m; };

    // Manual reorder + the drag-out zones (ui/projects/dragReorder.js).
    const { attachRowDrag, keyMeta, isDragging } = createDragReorder({
      list, overlay, app, close: () => close(), render: () => render(),
      sortMode: () => sortMode, setSortMode: (m) => setSortMode(m),
      sortItems: (items, mode) => sortItems(items, mode), buildItems: (o) => buildItems(o),
      loadOrder, saveOrder, confirmOpen, openRemote: (m) => openRemote(m),
      invalidateRemotes: () => invalidateRemotes(),
      beginRemoval, retireKey, localKey, remoteKey, rowById,
    });


    // A read-only row for an incognito session open in another tab.
    const makeIncognitoPeerRow = (p) => {
      const row = document.createElement('div');
      row.className = 'project-row project-incognito';
      const thumb = document.createElement('div');
      thumb.className = 'project-thumb project-thumb-placeholder';
      thumb.innerHTML = icon('incognito', { size: 24 });
      row.appendChild(thumb);
      const info = document.createElement('div');
      info.className = 'project-info';
      const name = document.createElement('div');
      name.className = 'project-name';
      name.textContent = p.name || 'Incognito (unsaved)';
      const sub = document.createElement('div');
      sub.className = 'project-sub';
      sub.textContent = 'Incognito · open in another tab';
      info.append(name, sub);
      row.appendChild(info);
      return row;
    };

    const emptyLabelFor = (mode) =>
      mode === 'incognito' ? 'No incognito tabs.'
        : mode === 'server' ? 'No server projects.'
          : mode === 'local' ? 'No local projects.'
            : mode === 'peer-open' ? 'Nothing open in another tab.'
              : mode === 'peer-closed' ? 'Every saved project is open in another tab.'
                : 'No saved projects yet.';

    // Bound once; the observer picks up each rebuild's rows itself.
    observeReveal(list, '.project-row');

    // Every row the current state would list, in order — the one source of truth for both
    // render() and the filter transition's key delta (createFilterAnimator).
    const rowPlan = () => {
      const q = search.value || '';
      const plan = [];
      const showIncog = filterMode === 'all' || filterMode === 'incognito';
      // The synthetic current-tab row, pinned above the sorted rows; in the incognito
      // filter only a real incognito session qualifies.
      if (showIncog && app.storage.temporary) {
        const label = app.storage.incognito ? 'incognito (unsaved)' : 'temporary (unsaved)';
        const qualifies = filterMode === 'incognito' ? app.storage.incognito : true;
        if (qualifies && rowMatches(label, q))
          plan.push({ key: 'temp', build: () => makeRow(null, { temp: true, incognito: app.storage.incognito }) });
      }
      // Incognito sessions open in other tabs, also pinned above the sorted rows.
      if (showIncog) {
        for (const p of incognitoPeers)
          if (rowMatches(p.name || 'Incognito', q))
            plan.push({ key: `peer:${p.peerId ?? p.name}`, build: () => makeIncognitoPeerRow(p) });
      }
      for (const it of sortItems(buildItems({ applySearch: true }), sortMode))
        plan.push({ key: it.key, item: it, build: it.build });
      return plan;
    };

    // The "before" side of a filter transition.
    let shownKeys = [];
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
      shownKeys = [];
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
          : (q.trim() ? 'No matching projects.' : emptyLabelFor(filterMode));
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

    const { open, close } = wireModalShell(overlay, openBtn, closeBtn, {
      // Re-fetch the server listing on each open.
      onOpen: () => { search.value = ''; clearSelection(); invalidateRemotes(); sortEl.value = sortMode; searchModeEl.value = searchMode; render(); }
    });

    // Every filter control re-lists through the same transition (createFilterAnimator): the
    // light filter effect, never the delete's scatter. A keystroke mid-animation re-renders
    // immediately instead of stacking a second leave.
    const runFilter = createFilterAnimator({
      keys: () => shownKeys,
      next: () => rowPlan().map((e) => e.key),
      render,
      find: rowByFilterKey,
    });
    attachSearchFilter(search, () => runFilter());
    filterEl.addEventListener('change', () => { filterMode = filterEl.value; runFilter(); });
    sortEl.value = sortMode;
    sortEl.addEventListener('change', () => { setSortMode(sortEl.value); runFilter(); });
    searchModeEl.value = searchMode;
    searchModeEl.addEventListener('change', () => { searchMode = searchModeEl.value; ssSet(SEARCH_MODE_KEY, searchMode); runFilter(); });

    // Batch actions over the checked rows (ui/projects/batchActions.js).
    wireBatchActions({
      app, batchBtns, sel, selected, selectables, doomed, clearSelection, allSelected,
      updateBatchBar, render: () => render(), pickServer, beginRemoval, rowById,
      invalidateRemotes: () => invalidateRemotes(),
    });

    // A fresh editor tab discards nothing, so there is nothing to confirm.
    newEditorBtn.addEventListener('click', () => {
      if (!window.open(location.origin + location.pathname, '_blank')) {
        notify('The browser blocked the new tab — allow pop-ups for this page', 'fail');
        return;
      }
      close();
    });
    clearAllBtn.addEventListener('click', async () => {
      const msg = hasServers()
        ? 'Are you sure? This permanently deletes ALL local projects. Server projects are not affected.'
        : 'Are you sure? This permanently deletes ALL saved projects.';
      const title = hasServers() ? 'Delete all local projects' : 'Delete all projects';
      if (!(await app.confirm(msg, { title, danger: true, confirmLabel: 'Yes', confirmIcon: 'trash', cancelLabel: 'No' }))) return;
      // The same leave every removal plays (desktop: ProjectsDialog::scatterRows). Real saved
      // rows only: the "temporary (unsaved)" row re-renders straight after.
      const rows = [...list.querySelectorAll('.project-row:not(.project-temp)')];
      const settle = beginRemoval();
      // The bar's controls leave with them: every selectable row is going.
      const keys = [...selectables.keys()];
      for (const k of keys) doomed.add(k);
      const leaving = Promise.all(rows.map((row, i) =>
        leaveThenRemove(row, () => {}, rowLeaveDust(rows.length, i, ITEM_DUST_MS))));
      selected.clear();
      updateBatchBar();
      await leaving;
      app.clearAllProjects();
      await settle();
      for (const k of keys) doomed.delete(k);
      updateBatchBar();
    });

    subscribe(EVENTS.connectionsChanged, () => {
      // The cached listing is stale; the next render re-fetches (never mid-drag or mid-removal).
      invalidateRemotes();
      if (mayRefresh()) render();
    });

    // Never mid-drag or mid-removal — the deferred render shows the recorded state.
    app.tabs.onProjectsChanged(() => { if (mayRefresh()) render(); });
    app.tabs.onPeers(ids => {
      peers = ids || [];
      if (mayRefresh()) render();
    });
    app.tabs.onIncognitoPeers(list => {
      incognitoPeers = list || [];
      if (mayRefresh()) render();
    });

    // On-open chooser: only tab AND saved projects exist. Skipped when launched to open a
    // specific project (?open=) or image (extension #stencil=) — the user already chose.
    // The count is the one the page OPENED with: the handshake below settles up to its
    // timeout later, and a project saved in between is work in progress to cover, not an
    // arrival to choose from.
    const openedWithSaved = store.list().length > 0;
    app.tabs.whenReady().then(({ youAreOnly }) => {
      // open(null): this one appears because the page opened, not the toolbar icon, so it
      // drops in from above.
      if (youAreOnly && openedWithSaved && !app.pendingOpenProjectId && !app.hasExternalLaunch) open(null);
    });
  }
}
define('stencil-projects-modal', StencilProjectsModal);
