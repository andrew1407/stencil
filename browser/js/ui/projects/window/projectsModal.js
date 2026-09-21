import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches, escapeHtml } from '../../base.js';
import { wireNameEditor, notify, isTouchLike, shortName, anchorPickerInput } from '../../../utils.js';
import { icon, setSelectAllFace } from '../../icons.js';
import { SORT_MODES, sortProjectItems } from './projectSort.js';
import {
  observeReveal, leaveThenRemove, wipeDurationMs, createFilterAnimator,
  materialize, filterDelta, rowDustGrid, ROW_ARRIVE_MS, ROW_ARRIVE_DELAY_MS,
  ITEM_DUST_MS, rowLeaveDust, revealControls, revealBar,
  SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS, markIn, markOut,
} from '../../motion.js';
import { normalizeHex } from '../../../core/settings/accents.js';
import { subscribe, EVENTS } from '../../../eventBus/appBus.js';

import { DOUBLE_CLICK_MS, DRAG_SLOP_PX, rowOpenIntent, createOpenGesture, canRefreshList } from '../../../core/project/projectOpenGesture.js';
import { createRemoteListing, showsRemoteSkeletons } from '../../../core/remote/remoteListing.js';
import { createProjectRowMenu } from './projectRowMenu.js';
import { createThumbZoom } from './projectThumbZoom.js';
import { projectsModalInner } from '../markup.js';
import { createRemoteRow } from '../row/remoteRow.js';
import { attachRowActions, attachIncognitoActions } from '../row/rowActions.js';
import { createDragReorder } from '../list/dragReorder.js';
import { wireBatchActions } from '../list/batchActions.js';
import { createProjectSelection } from '../list/selection.js';
import { createLocalRow } from '../row/localRow.js';
import { createListPrefs } from '../list/listPrefs.js';
import { createRowMeta } from '../row/rowMeta.js';
import { createColorPicker } from '../colorPicker.js';
import { createRowPlan } from '../row/rowPlan.js';
import { createRenderList } from '../list/renderList.js';
import { wireListActions } from '../list/listActions.js';
import { wireLiveRefresh } from '../list/liveRefresh.js';

// The projects chooser / switcher. Rows are built at runtime (the static #projects-list
// stays comment-only) to keep the markup tests green.
export class StencilProjectsModal extends StencilElement {
  static inner() { return projectsModalInner(); }
  static template() { return hostTag('stencil-projects-modal', 'id="projects-modal-overlay" class="app-modal-overlay"', StencilProjectsModal.inner()); }

  wire(app) {
    const $ = (id) => document.getElementById(id);
    const overlay = $('projects-modal-overlay');
    const search = $('projects-search');
    const list = $('projects-list');
    const clearAllBtn = $('projects-clear-all');
    const filterEl = $('projects-filter');
    const sortEl = $('projects-sort');
    const searchModeEl = $('projects-search-mode');
    const store = app.storage.store;

    let peers = [];
    let incognitoPeers = [];
    // The worker echoes every tab's active id, ours included; shared by the row badge and the
    // "Open elsewhere" filter so the two never disagree.
    const isPeerOpen = (m) => peers.includes(m.id) && m.id !== app.storage.activeId;
    let filterMode = 'all';
    const hasServers = () => !!app.connections?.urls?.length;

    const { setSortMode, setSearchMode, syncControls, ...prefs } = createListPrefs({ sortEl, searchModeEl });
    const { fmtDate, projectTooltip, expiryLabel } = createRowMeta(store);

    // One cached server listing per render cycle, so mixed/date sorts interleave from one
    // snapshot; invalidated on connection/server-project changes and on each open.
    const remotes = createRemoteListing(() => app.connections.remoteProjects());
    // Kept alive (not revoked on load) so the hover-magnify zoom can reuse them.
    const remoteObjectUrls = new Set();

    // Multi-select + the batch bar (ui/projects/selection.js).
    const {
      selected, doomed, selectables, batchBtns, sel, retireKey, localKey, remoteKey,
      isServerMeta, allSelected, updateBatchBar, clearSelection, toggleSelect,
    } = createProjectSelection({ batchBar: $('projects-batch-bar'), batchCount: $('projects-batch-count'), hasServers: () => hasServers() });

    // Pick a connected server (auto when only one); null when cancelled. `closeAnchor` is
    // where the dialog's dust pours back into (a row's "⋯", since the menu row is gone by then).
    const pickServer = async (message, closeAnchor = null) => {
      const urls = app.connections?.urls || [];
      if (!urls.length) return null;
      if (urls.length === 1) return urls[0];
      return app.choose(message, { title: 'Choose server', confirmLabel: 'OK', confirmIcon: 'server', closeAnchor, options: urls.map(u => ({ value: u, label: u })) });
    };

    const { enableThumbZoom, hideZoom } = createThumbZoom();
    const { showMenu, closeMenu } = createProjectRowMenu();

    // A delete plays the row out before the rebuild.
    const rowById = (id) => (id == null ? null : list.querySelector(`[data-id="${id}"]`));
    const render = () => rows.render();
    const beginRemoval = () => rows.beginRemoval();

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

    const openColorPicker = createColorPicker({ app, list, render });

    // One local project row (ui/projects/localRow.js); `render` and `close` cross as thunks.
    const makeRow = createLocalRow({
      app, close: () => close(), render,
      localKey, selected, selectables, isServerMeta, toggleSelect,
      enableThumbZoom, projectTooltip, fmtDate, expiryLabel, isPeerOpen, hasServers,
      pickServer, confirmOpen, scrollRowIntoView, openColorPicker, beginRemoval, retireKey,
      rowById, showMenu,
    });

    // One server-project row (ui/projects/remoteRow.js); `render`, `close`, `openRemote` and
    // `invalidateRemotes` are declared below, so they cross as thunks.
    const makeRemoteRow = createRemoteRow({
      app, close: () => close(), render,
      remoteKey, selected, selectables, toggleSelect,
      enableThumbZoom, showMenu, remoteObjectUrls,
      projectTooltip, fmtDate, confirmOpen, openRemote: (m) => openRemote(m),
      scrollRowIntoView, beginRemoval, retireKey, rowById,
      invalidateRemotes: () => invalidateRemotes(),
    });

    // Shared with the external-launch server hand-off (DrawingApp.openRemoteProject).
    const openRemote = (meta) => app.openRemoteProject(meta);

    // A skipped render is picked up by the settle render / endDrag's render instead.
    const mayRefresh = () => canRefreshList({
      open: overlay.classList.contains('modal-open'),
      dragging: isDragging(),
      removing: rows.removing(),
    });

    // ensureRemotes() fills the cache once, then re-renders (deferred while a wipe or drag
    // holds). Stale-fetch dropping lives in the factory, where it is unit-tested.
    const ensureRemotes = () => {
      if (!showsServer() || !hasServers()) return;
      remotes.ensure(() => { if (mayRefresh()) render(); });
    };
    const invalidateRemotes = () => remotes.invalidate();

    const { buildItems, sortItems, rowPlan, showsServer } = createRowPlan({
      app, store, search, prefs, remotes, filterMode: () => filterMode,
      isPeerOpen, isServerMeta, makeRow, makeRemoteRow, incognitoPeers: () => incognitoPeers,
    });

    // Manual reorder + the drag-out zones (ui/projects/dragReorder.js).
    const { attachRowDrag, keyMeta, isDragging } = createDragReorder({
      list, overlay, app, close: () => close(), render,
      sortMode: () => prefs.sortMode(), setSortMode: (m) => setSortMode(m),
      sortItems, buildItems,
      loadOrder: prefs.loadOrder, saveOrder: prefs.saveOrder, confirmOpen, openRemote: (m) => openRemote(m),
      invalidateRemotes: () => invalidateRemotes(),
      beginRemoval, retireKey, localKey, remoteKey, rowById,
    });

    // Bound once; the observer picks up each rebuild's rows itself.
    observeReveal(list, '.project-row');

    const rows = createRenderList({
      app, list, search, clearAllBtn, remotes, remoteObjectUrls, rowPlan, showsServer,
      filterMode: () => filterMode, hasServers, ensureRemotes, keyMeta, attachRowDrag,
      hideZoom, closeMenu, selected, selectables, updateBatchBar,
    });
    const { shownKeys, rowByFilterKey } = rows;

    const { open, close } = wireModalShell(overlay, $('projects-btn'), $('projects-close'), {
      // Re-fetch the server listing on each open.
      onOpen: () => { search.value = ''; clearSelection(); invalidateRemotes(); syncControls(); render(); },
      focusOnOpen: search,
    });

    // Every filter control re-lists through the same transition (createFilterAnimator): the light
    // filter effect, never the delete's scatter. A keystroke mid-animation re-renders at once.
    const runFilter = createFilterAnimator({
      keys: () => shownKeys,
      next: () => rowPlan().map((e) => e.key),
      render,
      find: rowByFilterKey,
    });
    attachSearchFilter(search, () => runFilter());
    filterEl.addEventListener('change', () => { filterMode = filterEl.value; runFilter(); });
    sortEl.addEventListener('change', () => { setSortMode(sortEl.value); runFilter(); });
    searchModeEl.addEventListener('change', () => { setSearchMode(searchModeEl.value); runFilter(); });

    // Batch actions over the checked rows (ui/projects/batchActions.js).
    wireBatchActions({
      app, batchBtns, sel, selected, selectables, doomed, clearSelection, allSelected,
      updateBatchBar, render, pickServer, beginRemoval, rowById,
      invalidateRemotes: () => invalidateRemotes(),
    });

    wireListActions({
      app, list, hasServers, selected, selectables, doomed, updateBatchBar, beginRemoval,
      close: () => close(), els: { newEditorBtn: $('projects-new-editor'), clearAllBtn },
    });
    wireLiveRefresh({
      app, store, mayRefresh, render, invalidateRemotes, open,
      setPeers: (ids) => { peers = ids; }, setIncognitoPeers: (l) => { incognitoPeers = l; },
    });
  }
}
define('stencil-projects-modal', StencilProjectsModal);
