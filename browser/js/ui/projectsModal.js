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
import { subscribe, EVENTS } from '../bus/appBus.js';

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

// ── Component: projects chooser / switcher modal ────────────────
// Lists saved projects + a synthetic row for the current temp editor. Rows built at
// runtime (static #projects-list stays comment-only) to keep the markup tests green.
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

    let peers = []; // active project ids open in OTHER tabs
    let incognitoPeers = []; // incognito sessions open in OTHER tabs ({ peerId, name, updatedAt })
    // The worker echoes every tab's active id (including ours), so exclude this tab's own
    // active project — only true when a DIFFERENT tab has it. Shared by the row badge and
    // the "Open elsewhere" / "Not open elsewhere" filter so the two can never disagree.
    const isPeerOpen = (m) => peers.includes(m.id) && m.id !== app.storage.activeId;
    let filterMode = 'all';
    const hasServers = () => !!app.connections?.urls?.length;

    // ── Sort mode + per-session manual drag order ──
    // Both persist in sessionStorage: they survive a reload but reset when the tab session
    // ends, and never touch the shared localStorage registry (which has a C++ core twin under
    // the parity contract). Default sort is by-name-mixed (local + server interleaved).
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

    // Cached server-project listing for this render cycle (createRemoteListing above),
    // so mixed/date sort modes interleave server + local rows from one snapshot;
    // invalidated on connection/server-project changes and on each modal open.
    const remotes = createRemoteListing(() => app.connections.remoteProjects());
    // Live blob URLs for the current render's remote thumbnails. Kept alive (not revoked on
    // load) so the hover-magnify zoom can reuse them; freed at the start of the next render.
    const remoteObjectUrls = new Set();

    // ── Multi-select + the batch bar (ui/projects/selection.js) ──
    const {
      selected, doomed, selectables, batchBtns, sel, retireKey, localKey, remoteKey,
      isServerMeta, anyLiveSelectable, allSelected, updateBatchBar, updateSelectAll,
      clearSelection, toggleSelect,
    } = createProjectSelection({ batchBar, batchCount, hasServers: () => hasServers() });

    // Pick a connected server (auto when only one). Returns an address or null (cancelled).
    // `closeAnchor` (also in confirmOpen/openWithIntent) is where the dialog's dust pours
    // back into — a row's "⋯" button, since the menu row it grew out of is gone by then.
    // Omitted for the row-gesture paths, which fly back into the gesture's own point.
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

    // Row hover tooltip (local + server rows): the picture's size, its orientation under
    // it, and the description when there is one. No drawn-line length — a number nobody
    // hovers a project row to read, and it made the tip a third taller for it.
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

    // Magnified hover preview that follows the cursor — ui/projectThumbZoom.js.
    const { enableThumbZoom, hideZoom } = createThumbZoom();

    // One floating "⋯" menu reused by every row — ui/projectRowMenu.js.
    const { showMenu, closeMenu } = createProjectRowMenu();

    // The rendered row for a project id — a delete plays it out before the rebuild.
    const rowById = (id) => (id == null ? null : list.querySelector(`[data-id="${id}"]`));

    // Call BEFORE a removal; await what it returns after. leaveThenRemove resolves on the
    // short box-collapse while the ash falls much longer, so the list HEIGHT (it sizes the
    // modal) and the re-render are both held until wipeDurationMs; out-of-band refresh
    // triggers hold off too (canRefreshList).
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
        // What the removal REVEALED — the pinned "Temporary (unsaved)" row, when the
        // project that just left was the one open here — MATERIALIZES: it waits behind
        // its own motes and comes up as they land, the removal played backwards (the
        // connections list's arrival, on this list's grain). It used to be simply there
        // on the next frame (user report). Only rows the settle ADDED; everything that
        // was already listed stays where it is. Desktop twin: ListFilterFade::dustRowIn
        // over the rows a ProjectsDialog::refresh rebuild brought in.
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

    // Opening replaces this tab's unsaved session (or spawns a tab), so every open path
    // confirms first. Returns true to proceed; `newTab` tunes the wording.
    const confirmOpen = (name, newTab = false, closeAnchor = null) => app.confirm(
      newTab
        ? `Open "${shortName(name || 'Untitled')}" in a new tab?`
        : `Open "${shortName(name || 'Untitled')}" here? Any unsaved changes in the current tab will be replaced.`,
      { title: 'Open project', confirmLabel: 'Open', confirmIcon: 'folder', cancelLabel: 'Cancel', closeAnchor });

    // ONE hidden colour field for the whole list, re-pointed at the row being recoloured.
    // Never `display: none` and never built inside the click handler: the native picker
    // opens beside its input's laid-out box, and one that has none lands at the page
    // corner — utils.anchorPickerInput puts it under the button that was pressed.
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
    // Beside the list, never inside it: render() wipes the list's own innerHTML.
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

    // One LOCAL project row — ui/projects/localRow.js. Same thunk contract as makeRemoteRow
    // below: `render` and `close` are declared further down in wire().
    const makeRow = createLocalRow({
      app, close: () => close(), render: () => render(),
      localKey, selected, selectables, isServerMeta, toggleSelect,
      enableThumbZoom, projectTooltip, fmtDate, expiryLabel, isPeerOpen, hasServers,
      pickServer, confirmOpen, scrollRowIntoView, openColorPicker, beginRemoval, retireKey,
      rowById, showMenu,
    });

    // One server-project row — ui/projects/remoteRow.js.
    // `render`, `close`, `openRemote` and `invalidateRemotes` are declared below, so they
    // cross as thunks — the row calls them long after wire() has finished.
    const makeRemoteRow = createRemoteRow({
      app, close: () => close(), render: () => render(),
      remoteKey, selected, selectables, toggleSelect,
      enableThumbZoom, showMenu, remoteObjectUrls,
      projectTooltip, fmtDate, confirmOpen, openRemote: (m) => openRemote(m),
      scrollRowIntoView, beginRemoval, retireKey, rowById,
      invalidateRemotes: () => invalidateRemotes(),
    });

    // Fetch a remote project's image + layout and load it into the editor (shared with
    // the external-launch server hand-off — see DrawingApp.openRemoteProject).
    const openRemote = (meta) => app.openRemoteProject(meta);

    // A shimmering placeholder row shaped like a project row, shown while the server
    // listing loads so the modal opens instantly instead of waiting on the network.
    const makeSkeletonRow = () => {
      const row = document.createElement('div');
      row.className = 'project-row project-skeleton';
      row.innerHTML = '<div class="project-thumb skel"></div>'
        + '<div class="project-info"><div class="skel skel-line"></div>'
        + '<div class="skel skel-line short"></div></div>';
      return row;
    };

    // May an out-of-band trigger rebuild the list right now? (See canRefreshList — a
    // skipped render is picked up by the settle render / endDrag's render instead.)
    const mayRefresh = () => canRefreshList({
      open: overlay.classList.contains('modal-open'),
      dragging: isDragging(),
      removing: removalsInFlight > 0,
    });

    // ensureRemotes() fills the cache once, then re-renders — deferred to the
    // settle/endDrag render while a wipe or drag holds (mayRefresh). Stale-fetch dropping
    // lives in the factory, where it is unit-tested.
    const showsServer = () => filterMode === 'all' || filterMode === 'server';
    const ensureRemotes = () => {
      if (!showsServer() || !hasServers()) return;
      remotes.ensure(() => { if (mayRefresh()) render(); });
    };
    const invalidateRemotes = () => remotes.invalidate();

    // ── Build one flat, sortable item list (local + cached server rows) ──
    // Each item carries a stable key, a lowercased name + a date for the comparators, an
    // isRemote flag, and a build() that returns the row element (reusing makeRow/makeRemoteRow).
    const localRowKey = (m) => `local:${m.id}`;
    const remoteRowKey = (m) => `remote:${m.serverUrl}:${m.id}`;
    const metaName = (m) => (m.name || '').toLowerCase();
    const metaDate = (m) => m.updatedAt || m.createdAt || 0;
    // "Open elsewhere" / "Not open elsewhere" are LOCAL-only scopes, like "Local" itself —
    // a not-yet-claimed remote-cache row (makeRemoteRow) has no peers relationship to filter on.
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
      // Server (golden) rows from the cache, deduped against server-linked local rows.
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

    // ── Dragging a row: manual reorder + the drag-out zones (ui/projects/dragReorder.js) ──
    const { attachRowDrag, keyMeta, isDragging } = createDragReorder({
      list, overlay, app, close: () => close(), render: () => render(),
      sortMode: () => sortMode, setSortMode: (m) => setSortMode(m),
      sortItems: (items, mode) => sortItems(items, mode), buildItems: (o) => buildItems(o),
      loadOrder, saveOrder, confirmOpen, openRemote: (m) => openRemote(m),
      invalidateRemotes: () => invalidateRemotes(),
      beginRemoval, retireKey, localKey, remoteKey, rowById,
    });


    // A read-only row for an incognito session open in ANOTHER tab (informational — its
    // in-memory content can't be reached from here).
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

    // Rows fade + lift through the scroller as it scrolls. Bound once; the observer
    // picks up each rebuild's rows itself, so render() stays untouched.
    observeReveal(list, '.project-row');

    // Every row the current state would list, in order — the one source of truth for
    // both render() and the filter transition's key delta (createFilterAnimator), so
    // the two can never disagree about what a filter/sort/search change moves.
    const rowPlan = () => {
      const q = search.value || '';
      const plan = [];
      const showIncog = filterMode === 'all' || filterMode === 'incognito';
      // Synthetic current-tab temporary/incognito row (pinned at the top, above the sorted
      // rows). In the incognito filter only a real incognito session qualifies.
      if (showIncog && app.storage.temporary) {
        const label = app.storage.incognito ? 'incognito (unsaved)' : 'temporary (unsaved)';
        const qualifies = filterMode === 'incognito' ? app.storage.incognito : true;
        if (qualifies && rowMatches(label, q))
          plan.push({ key: 'temp', build: () => makeRow(null, { temp: true, incognito: app.storage.incognito }) });
      }
      // Incognito sessions open in OTHER tabs (read-only) — also pinned above the sorted rows.
      if (showIncog) {
        for (const p of incognitoPeers)
          if (rowMatches(p.name || 'Incognito', q))
            plan.push({ key: `peer:${p.peerId ?? p.name}`, build: () => makeIncognitoPeerRow(p) });
      }
      // Local + server rows as one sorted, drag-reorderable list per the active sort mode.
      for (const it of sortItems(buildItems({ applySearch: true }), sortMode))
        plan.push({ key: it.key, item: it, build: it.build });
      return plan;
    };

    // Keys the LAST render actually listed — the "before" side of a filter transition.
    let shownKeys = [];
    const rowByFilterKey = (key) =>
      list.querySelector(`[data-filter-key="${String(key).replace(/["\\]/g, '\\$&')}"]`);

    const render = () => {
      const q = search.value || '';
      hideZoom();
      closeMenu();   // a rebuilt list invalidates any open row menu
      selectables.clear();   // repopulated below by every row that wires a checkbox
      // Free the previous render's remote thumbnail blob URLs (kept alive for the hover-zoom).
      for (const u of remoteObjectUrls) URL.revokeObjectURL(u);
      remoteObjectUrls.clear();
      list.innerHTML = '';
      const showServer = showsServer();

      // Kick off (or reuse) the cached server listing before the plan reads it.
      if (showServer && hasServers()) ensureRemotes();
      keyMeta.clear();
      shownKeys = [];
      for (const entry of rowPlan()) {
        const row = entry.build();
        // Stamped on EVERY row (the synthetic ones too) so the filter transition can
        // find the rows a change drops or reveals.
        row.dataset.filterKey = entry.key;
        if (entry.item) { keyMeta.set(entry.key, entry.item); attachRowDrag(row, entry.key); }
        list.appendChild(row);
        shownKeys.push(entry.key);
      }

      // Shimmer skeletons after the sorted rows — but ONLY while a fetch is genuinely in
      // flight (showsRemoteSkeletons); on the bare null cache nothing would ever fill them.
      const loadingRemotes = showsRemoteSkeletons({
        showServer, hasServers: hasServers(), cache: remotes.cache, loading: remotes.loading,
      });
      if (loadingRemotes) { list.appendChild(makeSkeletonRow()); list.appendChild(makeSkeletonRow()); }

      // Nothing to show (and not still loading) → an honest empty / error message.
      if (!list.querySelector('.project-row:not(.project-skeleton)') && !loadingRemotes) {
        const empty = document.createElement('div');
        empty.className = 'info-empty';
        empty.textContent = remotes.failed ? 'Could not reach server.'
          : (q.trim() ? 'No matching projects.' : emptyLabelFor(filterMode));
        list.appendChild(empty);
      }

      // "Clear All" only ever wipes local projects. When a server is connected, say so
      // explicitly ("Clear All Local") so the label matches the actual removal.
      const clearAllLabel = clearAllBtn.querySelector('span');
      if (clearAllLabel) clearAllLabel.textContent = hasServers() ? 'Clear All Local' : 'Clear All';
      clearAllBtn.dataset.title = hasServers()
        ? 'Delete every local project (server projects are not affected)'
        : 'Delete every saved project';
      // …and only when there IS one. The synthetic "temporary (unsaved)" row is not a saved
      // project — with just that on screen, Clear All wiped nothing and the row came straight
      // back, which read as a removal that undid itself.
      const clearable = list.querySelectorAll('.project-row:not(.project-temp):not(.project-remote):not(.project-skeleton)').length;
      clearAllBtn.disabled = clearable === 0;
      clearAllBtn.dataset.disabledReason = 'No saved projects to clear';

      // Drop selections whose project is GONE — removed here, or from another tab: the
      // bar reads `selected.size`, so a dead key kept "1 selected" on screen over an empty
      // list (user report). Against what the app KNOWS, never the filtered view — filtering
      // a row out of sight must not silently drop it from a pending batch.
      // (Deleting the key being visited is safe on a Map, so this needs no copy.)
      for (const [key, entry] of selected) {
        if (entry?.kind === 'local' && entry.id != null && !app.storage.store.getMeta(entry.id))
          selected.delete(key);
      }
      updateBatchBar();
    };

    const { open, close } = wireModalShell(overlay, openBtn, closeBtn, {
      // Re-fetch the server listing on each open so a freshly-opened modal is current.
      onOpen: () => { search.value = ''; clearSelection(); invalidateRemotes(); sortEl.value = sortMode; searchModeEl.value = searchMode; render(); }
    });

    // Every filter control re-lists through the SAME symmetric transition: what the
    // change drops collapses out, then the rebuild, then what it reveals fades in — the
    // light filter effect, never the delete's scatter (nothing here was removed). Typing
    // is safe: a keystroke landing mid-animation re-renders immediately instead of
    // stacking a second leave (createFilterAnimator).
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

    // ── Batch actions over the checked rows (ui/projects/batchActions.js) ──
    wireBatchActions({
      app, batchBtns, sel, selected, selectables, doomed, clearSelection, allSelected,
      updateBatchBar, render: () => render(), pickServer, beginRemoval, rowById,
      invalidateRemotes: () => invalidateRemotes(),
    });

    // Opens a fresh editor tab; nothing is discarded here, so nothing to confirm.
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
      // The whole list comes apart before it empties — the same leave every other removal
      // plays, and the desktop's ProjectsDialog::scatterRows. Real saved rows only: the
      // "temporary (unsaved)" row re-renders straight after, which read as an undone removal.
      const rows = [...list.querySelectorAll('.project-row:not(.project-temp)')];
      const settle = beginRemoval();
      // The bar's controls leave WITH them (batch remove says why): every selectable row
      // is going, so Select all, the count and the batch buttons have nothing left.
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
      // A connect/disconnect or live server project-event invalidates the cached listing, so
      // the next render re-fetches it — never mid-drag or mid-removal (mayRefresh).
      invalidateRemotes();
      if (mayRefresh()) render();
    });

    // Re-render when another tab changes the project set or peer activity (never mid-drag
    // or mid-removal — the state is still recorded; the deferred render shows it).
    app.tabs.onProjectsChanged(() => { if (mayRefresh()) render(); });
    app.tabs.onPeers(ids => {
      peers = ids || [];
      if (mayRefresh()) render();
    });
    // Incognito sessions open in OTHER tabs (for the "Incognito tabs" filter).
    app.tabs.onIncognitoPeers(list => {
      incognitoPeers = list || [];
      if (mayRefresh()) render();
    });

    // On-open chooser: only if this is the only tab AND saved projects exist. Skipped when
    // launched to open a specific project (?open= deep link) or image (extension #stencil=
    // hand-off) — the user already chose; don't pop over it.
    app.tabs.whenReady().then(({ youAreOnly }) => {
      // open(null), not open(): this one appears because the PAGE opened, not because the
      // toolbar icon was pressed — so it drops in from above like the collapsing top menu
      // instead of flying out of an icon nobody touched.
      if (youAreOnly && store.list().length && !app.pendingOpenProjectId && !app.hasExternalLaunch) open(null);
    });
  }
}
define('stencil-projects-modal', StencilProjectsModal);
