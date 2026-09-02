import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches, escapeHtml } from './base.js';
import { wireNameEditor, notify, cmToUnit, unitLabel, isTouchLike, pointInRect, shortName, placeNearCursor } from '../utils.js';
import { icon } from './icons.js';
import { SORT_MODES, sortProjectItems, reconcileManualOrder } from './projectSort.js';
import { setTranslucentDragImage } from './dragGhost.js';
import { makeTouchDraggable } from './touchDrag.js';
import {
  observeReveal, leaveThenRemove, wipeDurationMs, scatterGridFor, createFilterAnimator,
  surfaceIn, surfaceOut, settleSurface, rectCenter, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS,
  TIP_DUST_IN_MS, TIP_DUST_OUT_MS, markIn, markOut,
} from './motion.js';
import { normalizeUrl } from '../net/connectionManager.js';
import { loadSavedServers } from '../net/connectionStore.js';

// ── Opening a project row: gesture → intent ─────────────────────────────────
// MOUSE   click            → confirm, this tab      dblclick        → open now, this tab
//         ⌘/Ctrl+click     → confirm, NEW TAB       ⌘/Ctrl+dblclick → open now, NEW TAB
//         Enter / Space    → confirm, this tab (⌘/Ctrl held → new tab), like a single click
// TOUCH   tap              → confirm, this tab      new tab → the row's ⋯ menu
//
// Touch deliberately has NO hold gesture: press-and-hold is the list's drag-to-reorder
// pickup (touchDrag.js), so the ⋯ menu is the touch route to a new tab. Movement always
// wins: past the slop the gesture belongs to the drag/scroll and any pending open is
// dropped. Pure — unit-tested.
export const DOUBLE_CLICK_MS = 250;    // single-click actions wait this long for a dblclick
export const DRAG_SLOP_PX = 10;        // travel that means "this is a drag/scroll, not a tap"
export const rowOpenIntent = ({ type, ctrlKey, metaKey } = {}) => {
  if (type === 'tap') return { confirm: true, target: 'here' };           // touch only
  const target = (ctrlKey || metaKey) ? 'newtab' : 'here';
  return { confirm: type !== 'dblclick', target };
};

// The gesture machine behind a row. The crux is the DEFERRED single click: a plain click
// waits one double-click interval and a dblclick cancels it, so the confirmation modal
// never flashes open and shut. Timers and the touch test are injected for DOM-free tests.
export const createOpenGesture = ({
  run,
  touch = () => isTouchLike(),
  delay = DOUBLE_CLICK_MS,
  slop = DRAG_SLOP_PX,
  setTimer = setTimeout,
  clearTimer = clearTimeout,
} = {}) => {
  let clickTimer = null;
  let press = null;          // { x, y, moved } while a pointer is down on the row
  let swallowNextClick = false;
  const cancelClick = () => { if (clickTimer !== null) { clearTimer(clickTimer); clickTimer = null; } };
  return {
    click(e = {}) {
      cancelClick();
      // The click synthesized after a long press (or a drag) must not also open.
      if (swallowNextClick) { swallowNextClick = false; return; }
      if (touch()) { run(rowOpenIntent({ type: 'tap' })); return; }
      clickTimer = setTimer(() => {
        clickTimer = null;
        run(rowOpenIntent({ type: 'click', ctrlKey: e.ctrlKey, metaKey: e.metaKey }));
      }, delay);
    },
    dblclick(e = {}) {
      cancelClick();                     // …the pending single click never happens
      if (touch()) return;               // touch has no double-click gesture
      run(rowOpenIntent({ type: 'dblclick', ctrlKey: e.ctrlKey, metaKey: e.metaKey }));
    },
    key(e = {}) {
      cancelClick();
      run(rowOpenIntent({ type: 'key', ctrlKey: e.ctrlKey, metaKey: e.metaKey }));
    },
    // ── Pointer travel: MOVEMENT WINS ──
    // Past the slop the gesture belongs to the drag/scroll: the pending open is dropped
    // and the click that may follow the release is swallowed. Nothing here ever OPENS
    // anything — the hold is the list's reorder pickup, not ours.
    pressStart(pt = {}) {
      swallowNextClick = false;
      press = { x: pt.x || 0, y: pt.y || 0, moved: false };
    },
    pressMove(pt = {}) {
      if (!press || press.moved) return false;
      if (Math.abs((pt.x || 0) - press.x) <= slop && Math.abs((pt.y || 0) - press.y) <= slop) return false;
      press.moved = true;
      cancelClick();                     // a deferred single click never survives a drag
      swallowNextClick = true;           // …nor does the click a drop may synthesize
      return true;
    },
    pressEnd() { const moved = !!press && press.moved; press = null; return moved; },
    // Drag pickup (HTML5 dragstart on mouse, the touch engine's onStart on finger).
    dragStart() { cancelClick(); swallowNextClick = true; press = null; },
    cancel() { cancelClick(); press = null; swallowNextClick = false; },
    get pendingClick() { return clickTimer !== null; },
    get dragging() { return !!press && press.moved; },
  };
};

// Whether an out-of-band change (server event, peers echo, another tab) may rebuild the
// list RIGHT NOW: never mid-drag (the rebuild destroys the dragged row) and never while a
// removal wipe plays — those wait for beginRemoval's settle render. Pure — unit-tested.
export const canRefreshList = ({ open = true, dragging = false, removing = false } = {}) =>
  !!open && !dragging && !removing;

// ── Server-listing cache (what the shimmer skeletons answer to) ─────────────
// null cache = not loaded, [] = loaded/empty. ensure() starts at most one fetch; a token
// drops a stale in-flight fetch after invalidate(), which must ALSO clear `loading` or
// ensure() never fetches again and the skeletons never resolve. Pure factory — unit-tested.
export const createRemoteListing = (load) => {
  const s = { cache: null, loading: false, failed: false, token: 0 };
  return {
    get cache() { return s.cache; },
    get loading() { return s.loading; },
    get failed() { return s.failed; },
    ensure(done = () => {}) {
      if (s.cache !== null || s.loading) return;
      s.loading = true;
      s.failed = false;
      const myToken = ++s.token;
      load()
        .then((list) => { if (myToken !== s.token) return; s.cache = list || []; s.loading = false; done(); })
        .catch(() => { if (myToken !== s.token) return; s.cache = []; s.failed = true; s.loading = false; done(); });
    },
    invalidate() { s.cache = null; s.failed = false; s.loading = false; s.token++; },
  };
};

// Skeleton rows may show ONLY while a server-listing fetch is genuinely in flight; a null
// cache with no fetch running must fall through to the honest empty/error state, or the
// skeletons stay up forever. Pure — unit-tested.
export const showsRemoteSkeletons = ({ showServer = false, hasServers = false, cache = null, loading = false } = {}) =>
  !!showServer && !!hasServers && cache === null && !!loading;

// Remote-thumbnail blob cache keyed by `serverUrl|id|version`, so the many re-renders
// (search keystrokes, live events, peer pings) reuse one fetch per project version
// instead of re-downloading on each. Mirrors the desktop ProjectsDialog::remoteThumbs_.
const remoteThumbCache = new Map();
const remoteThumbBlob = (conn, meta) => {
  const id = `${meta.serverUrl}|${meta.id}`;
  const key = `${id}|${meta.version ?? ''}`;
  let p = remoteThumbCache.get(key);
  if (!p) {
    // Drop any stale-version entry for this project so we hold ~one blob per project.
    for (const k of remoteThumbCache.keys())
      if (k.startsWith(`${id}|`)) remoteThumbCache.delete(k);
    // Fetch only files the record says exist (resultPath/originalPath), preferring the edited
    // `result`. A project with neither (no bytes uploaded) resolves to null with NO request, so
    // the console isn't spammed with 404s for files the server doesn't have.
    const hasResult = !!meta.resultPath;
    const hasOriginal = !!meta.originalPath;
    if (hasResult)
      p = conn.fetchFile(meta.id, 'result')
        .catch(() => (hasOriginal ? conn.fetchFile(meta.id, 'original') : null))
        .catch(() => null);
    else if (hasOriginal)
      p = conn.fetchFile(meta.id, 'original').catch(() => null);
    else
      p = Promise.resolve(null);
    remoteThumbCache.set(key, p);
  }
  return p;
};
// ── Component: projects chooser / switcher modal ────────────────
// Lists saved projects + a synthetic row for the current temp editor. Rows built at
// runtime (static #projects-list stays comment-only) to keep the markup tests green.
export class StencilProjectsModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('layers', { size: 18 })} Projects</h2>
                <button class="app-modal-close btn-icon-text" id="projects-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <!-- Search full-width on its own row; the filter selects sit on the row below
                 (user decision — the shared single-row bar squeezed the search box). -->
            <div class="modal-search-bar">
                <input type="text" id="projects-search" class="modal-search" placeholder="Search projects…">
            </div>
            <div class="modal-search-bar projects-filter-row">
                <select id="projects-filter" class="modal-filter" title="Filter projects">
                    <option value="all">All</option>
                    <option value="local">Local</option>
                    <option value="server">Server</option>
                    <option value="incognito">Incognito tabs</option>
                    <option value="peer-open">Open elsewhere</option>
                    <option value="peer-closed">Not open elsewhere</option>
                </select>
                <select id="projects-sort" class="modal-filter" title="Sort projects (drag a row to set a manual order)">
                    <option value="name">Name</option>
                    <option value="local">Local first</option>
                    <option value="server">Server first</option>
                    <option value="date-desc">Newest</option>
                    <option value="date-asc">Oldest</option>
                    <option value="manual">Manual order</option>
                </select>
                <select id="projects-search-mode" class="modal-filter" title="What the search box matches">
                    <option value="common">Name + keywords</option>
                    <option value="names">Names only</option>
                    <option value="keywords">Keywords only</option>
                </select>
            </div>
            <!-- Batch-select toolbar: appears once one or more rows are checked. -->
            <div class="projects-batch-bar" id="projects-batch-bar" style="display:none">
                <span class="projects-batch-count" id="projects-batch-count">0 selected</span>
                <span class="projects-batch-actions">
                    <button id="projects-select-all" class="btn-icon-text" style="display:none" title="Select every listed project (the current filter's rows)">${icon('check', { size: 13 })}<span>Select all</span></button>
                    <button id="projects-batch-move-server" class="btn-icon-text" title="Move the selected local projects to a server">${icon('server', { size: 13 })}<span>Move to server</span></button>
                    <button id="projects-batch-copy-server" class="btn-icon-text" title="Copy the selected local projects to a server">${icon('copy', { size: 13 })}<span>Copy to server</span></button>
                    <button id="projects-batch-move-local" class="btn-icon-text" title="Move the selected server projects to local">${icon('download', { size: 13 })}<span>Move to local</span></button>
                    <button id="projects-batch-copy-local" class="btn-icon-text" title="Copy the selected server projects to local">${icon('copy', { size: 13 })}<span>Copy to local</span></button>
                    <button id="projects-batch-remove" class="danger btn-icon-text" title="Remove the selected projects">${icon('trash', { size: 13 })}<span>Remove selected</span></button>
                    <button id="projects-batch-clear" class="btn-icon-text" title="Clear selection">${icon('x', { size: 13 })}<span>Clear</span></button>
                </span>
            </div>
            <div class="settings-body" id="projects-list"><!-- filled by JS --></div>
            <div class="settings-footer">
                <span class="footer-hint">Projects auto-save · unopened projects expire after 7 days</span>
                <button id="projects-blank-image" class="btn-icon-text" title="Create a blank image to draw on">${icon('image')}<span>Blank image</span></button>
                <button id="projects-new-editor" class="btn-icon-text" title="Open a new empty editor in another tab">${icon('plus-circle')}<span>New editor</span></button>
                <button id="projects-clear-all" class="danger btn-icon-text" title="Delete every saved project">${icon('trash')}<span>Clear All</span></button>
            </div>
        </div>
    `;
  }
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

    // ── Multi-select state ──
    // selected: key -> { kind:'local'|'remote', id, serverUrl, isServer, meta }. A local meta
    // with a remoteId+address is a server-backed project (isServer); a pure-local one isn't.
    const selected = new Map();
    const localKey = (id) => `local:${id}`;
    const remoteKey = (m) => `remote:${m.serverUrl}:${m.id}`;
    const isServerMeta = (m) => !!(m && m.remoteId && m.address);
    const sel = () => Array.from(selected.values());
    // Batch eligibility: move/copy-to-server wants only pure-local rows; move/copy-to-local
    // wants only pure-remote (not-yet-local) rows.
    const onlyLocalMovable = () => selected.size > 0 && sel().every(s => s.kind === 'local' && !s.isServer);
    const onlyRemoteMovable = () => selected.size > 0 && sel().every(s => s.kind === 'remote');

    const batchBtns = {
      moveServer: document.getElementById('projects-batch-move-server'),
      copyServer: document.getElementById('projects-batch-copy-server'),
      moveLocal: document.getElementById('projects-batch-move-local'),
      copyLocal: document.getElementById('projects-batch-copy-local'),
      remove: document.getElementById('projects-batch-remove'),
      clear: document.getElementById('projects-batch-clear'),
    };
    const updateBatchBar = () => {
      // The bar hosts Select all too, so it shows whenever the list HAS selectable
      // rows — the selection-only controls inside it come and go with the selection.
      batchBar.style.display = (selected.size || selectables.size) ? '' : 'none';
      batchCount.style.display = selected.size ? '' : 'none';
      batchCount.textContent = `${selected.size} selected`;
      const local = onlyLocalMovable();
      const remote = onlyRemoteMovable();
      // Inapplicable directions are HIDDEN, not greyed: a local-only selection never
      // moves "to local", so a disabled button is just noise.
      const show = (btn, on) => { btn.style.display = on ? '' : 'none'; };
      show(batchBtns.moveServer, local && hasServers());
      show(batchBtns.copyServer, local && hasServers());
      show(batchBtns.moveLocal, remote);
      show(batchBtns.copyLocal, remote);
      show(batchBtns.remove, selected.size > 0);
      show(batchBtns.clear, selected.size > 0);
      updateSelectAll();
    };
    const clearSelection = () => { selected.clear(); updateBatchBar(); };
    // What THIS render offered a checkbox for (the filtered view) — the select-all pool.
    const selectables = new Map();
    const allSelected = () =>
      selectables.size > 0 && [...selectables.keys()].every((k) => selected.has(k));
    const updateSelectAll = () => {
      const btn = document.getElementById('projects-select-all');
      if (!btn) return;
      btn.style.display = selectables.size ? '' : 'none';
      const label = btn.querySelector('span');
      if (label) label.textContent = allSelected() ? 'Deselect all' : 'Select all';
    };
    const toggleSelect = (key, entry, on) => {
      if (on) selected.set(key, entry);
      else selected.delete(key);
      updateBatchBar();
    };

    // Pick a connected server (auto when only one). Returns an address or null (cancelled).
    const pickServer = async (message) => {
      const urls = app.connections?.urls || [];
      if (!urls.length) return null;
      if (urls.length === 1) return urls[0];
      return app.choose(message, { title: 'Choose server', confirmLabel: 'OK', confirmIcon: 'server', options: urls.map(u => ({ value: u, label: u })) });
    };

    const fmtDate = ts => {
      if (!ts) return '';
      try {
        return new Date(ts).toLocaleString();
      } catch {
        return '';
      }
    };

    // Row hover tooltip (local + server rows). The line length is read from the cached
    // `lineLengthCm` on the registry meta (computed at save time), so we never reload a
    // project's image-heavy payload just to measure it.
    const projectTooltip = meta => {
      const lines = [];
      const w = meta.imageW;
      const h = meta.imageH;
      if (w && h) lines.push(`${w}x${h} px · ${h >= w ? 'portrait' : 'landscape'}`);
      if (meta.lineLengthCm > 0) {
        lines.push(`Line: ${(+cmToUnit(meta.lineLengthCm, app.unit)).toFixed(1)} ${unitLabel(app.unit)}`);
      }
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

    // Magnified hover preview: a fixed-position floating copy of a row's thumbnail that
    // follows the cursor, shown while hovering a thumb that holds a real image (not the
    // placeholder glyph). One reused element, shared by local + remote rows.
    const PREVIEW_ZOOM = 1.67;
    // A hover preview is a GLANCE, not a lightbox — it must leave the list underneath
    // readable. Mirrored by the max-width/max-height backstop in components.css.
    const PREVIEW_MAX_VW = 0.25;
    const PREVIEW_MAX_VH = 0.20;
    let zoomEl = null;
    const ensureZoom = () => {
      if (zoomEl) return zoomEl;
      zoomEl = document.createElement('div');
      zoomEl.className = 'project-thumb-zoom';
      zoomEl.innerHTML = '<img alt="">';
      document.body.appendChild(zoomEl);
      return zoomEl;
    };
    // ── The zoom is sand too (js/ui/motion.js surfaceIn/surfaceOut), on the shared
    // short tip clock — a sweep across rows re-triggers it fast. ──
    const ZOOM_DUST_IN_MS = TIP_DUST_IN_MS;
    const ZOOM_DUST_OUT_MS = TIP_DUST_OUT_MS;
    let zoomPoint = null;   // the thumbnail's own centre — the flight's origin/destination
    const hideZoom = () => {
      if (zoomEl) {
        if (zoomEl.style.display !== 'none') surfaceOut(zoomEl, zoomPoint, { ms: ZOOM_DUST_OUT_MS });
        else settleSurface(zoomEl);
        zoomEl.style.display = 'none';
        zoomSize = null;
      }
      zoomPoint = null;
    };
    // Switching window never fires the row's mouseleave — hide on blur, or the
    // zoom is still up when the user comes back (chatView hideThumbPreview parity).
    window.addEventListener('blur', hideZoom);
    // Holding Alt doubles the glance (every hover preview honours it — chat thumbs,
    // extension, desktop): factor 2 on the zoom cap AND the viewport ceilings.
    let zoomSize = null;   // {nw, nh} of the picture currently zoomed
    let zoomAlt = false;
    // A keyup can be lost off-window — Alt released while a docked DevTools pane (or any
    // other panel) holds keyboard focus never reaches this listener — which would leave
    // the NEXT hover's glance stuck doubled with no key actually held. blur is the one
    // signal that always fires when focus leaves, so it's the backstop that un-sticks it.
    window.addEventListener('blur', () => { zoomAlt = false; });
    // The size change IS a re-formation, not just a resize — replay the gather so
    // holding/releasing Alt reads as sand rather than a snap.
    const replayZoomDust = () => {
      if (!zoomEl || zoomEl.style.display === 'none' || !zoomPoint) return;
      surfaceIn(zoomEl, zoomPoint, { ms: ZOOM_DUST_IN_MS });
    };
    const applyZoomScale = () => {
      if (!zoomEl || !zoomSize || zoomEl.style.display === 'none') return;
      const f = zoomAlt ? 2 : 1;
      const scale = Math.min(PREVIEW_ZOOM * f,
        (window.innerWidth * PREVIEW_MAX_VW * f) / zoomSize.nw,
        (window.innerHeight * PREVIEW_MAX_VH * f) / zoomSize.nh);
      // The xl class doubles the CSS vw/vh caps too — they would clamp the explicit
      // width right back to the un-Alt size otherwise.
      zoomEl.classList.toggle('project-thumb-zoom-xl', zoomAlt);
      const img = zoomEl.querySelector('img');
      img.style.width = `${Math.round(zoomSize.nw * scale)}px`;
      img.style.height = `${Math.round(zoomSize.nh * scale)}px`;
    };
    window.addEventListener('keydown', e => { if (e.key === 'Alt') { zoomAlt = true; applyZoomScale(); replayZoomDust(); } });
    window.addEventListener('keyup', e => { if (e.key === 'Alt') { zoomAlt = false; applyZoomScale(); replayZoomDust(); } });
    // A backstop under applyZoomScale's own vw/vh caps: those track the window at the
    // moment a hover STARTS, so the box is re-cropped into whatever the window actually
    // is — applied on show and on resize, not per mousemove (a style write before every
    // measure forced a layout per move).
    const ZOOM_EDGE = 8;
    const applyZoomCaps = () => {
      if (!zoomEl) return;
      zoomEl.style.maxWidth = `${Math.max(0, window.innerWidth - ZOOM_EDGE * 2)}px`;
      zoomEl.style.maxHeight = `${Math.max(0, window.innerHeight - ZOOM_EDGE * 2)}px`;
    };
    window.addEventListener('resize', applyZoomCaps);
    // Down-right of the cursor, flipped/clamped into the viewport (shared helper); an
    // overflowing height pins to the bottom edge rather than flipping above.
    const positionZoom = e => {
      if (!zoomEl) return;
      placeNearCursor(zoomEl, e.clientX, e.clientY, { edge: ZOOM_EDGE, clampY: true });
    };
    const enableThumbZoom = thumbEl => {
      thumbEl.addEventListener('mouseenter', e => {
        const img = thumbEl.querySelector('img');
        if (!img || !img.src) return;   // placeholder glyph — nothing to magnify
        const z = ensureZoom();
        const zImg = z.querySelector('img');
        zImg.src = img.src;
        // Sized here rather than left to the CSS caps: independent max-width/max-height
        // clamping squashes a portrait thumb into a letterboxed landscape box. One scale
        // factor keeps aspect, honours the viewport ceiling, never upscales past PREVIEW_ZOOM.
        zoomSize = {
          nw: img.naturalWidth || img.width || 160,
          nh: img.naturalHeight || img.height || 160,
        };
        zoomAlt = e.altKey;   // Alt already held on entry counts too
        zoomPoint = rectCenter(thumbEl);
        z.style.display = 'block';
        applyZoomCaps();
        applyZoomScale();
        positionZoom(e);
        surfaceIn(z, zoomPoint, { ms: ZOOM_DUST_IN_MS });
      });
      thumbEl.addEventListener('mousemove', positionZoom);
      thumbEl.addEventListener('mouseleave', hideZoom);
    };

    // ── Per-row overflow ("⋯") menu ───────────────────────────────
    // A single floating menu reused by every row, so secondary actions (new tab,
    // rename, renew, move-to-server/local, remove) live behind one "⋯" button
    // instead of crowding the row. Closes on click-away, Escape, or re-render.
    let openMenu = null;
    let menuPoint = null;   // the "⋯" (or the right-click) the menu grew out of
    const closeMenu = () => {
      if (!openMenu) return;
      // Back into that same point as dust (js/ui/motion.js) — its own layer, so the
      // menu node still goes away NOW and nothing can be left half-removed.
      surfaceOut(openMenu, menuPoint, { ms: SURFACE_MENU_OUT_MS });
      openMenu.remove();
      openMenu = null;
      document.removeEventListener('mousedown', onMenuDocDown, true);
      document.removeEventListener('keydown', onMenuKey, true);
    };
    const onMenuDocDown = e => { if (openMenu && !openMenu.contains(e.target)) closeMenu(); };
    const onMenuKey = e => { if (e.key === 'Escape') { e.stopPropagation(); closeMenu(); } };
    // Opens under `anchor` (the "⋯" button), or at `point` ({x,y}) for a right-click.
    const showMenu = (anchor, items, point = null) => {
      closeMenu();
      const menu = document.createElement('div');
      menu.className = 'project-menu';
      for (const it of items) {
        if (!it) continue;   // skip conditionally-omitted entries
        const b = document.createElement('button');
        // Dedicated danger class (NOT the global `.danger`, which fills the button
        // red) so the destructive item is red TEXT on the menu background.
        b.className = 'project-menu-item btn-icon-text' + (it.danger ? ' is-danger' : '');
        b.innerHTML = `${icon(it.icon, { size: 15 })}<span>${it.label}</span>`;
        b.addEventListener('click', e => { e.stopPropagation(); closeMenu(); it.onClick(); });
        menu.appendChild(b);
      }
      document.body.appendChild(menu);
      const mw = menu.offsetWidth;
      const mh = menu.offsetHeight;
      let x;
      let y;
      if (point) {
        // Cursor-anchored (right-click): open at the point, flipping left/up near edges.
        x = point.x + mw > window.innerWidth - 8 ? point.x - mw : point.x;
        y = point.y + mh > window.innerHeight - 8 ? point.y - mh : point.y;
      } else {
        // Button-anchored: right-align under the "⋯", flip above if it would clip.
        const r = anchor.getBoundingClientRect();
        x = r.right - mw;
        y = r.bottom + 6;
        if (y + mh > window.innerHeight - 8) y = r.top - mh - 6;
      }
      menu.style.left = `${Math.max(8, x)}px`;
      menu.style.top = `${Math.max(8, y)}px`;
      openMenu = menu;
      // Grow out of the control that opened it: the cursor for a right-click, the "⋯"
      // button's centre otherwise.
      menuPoint = point || rectCenter(anchor);
      surfaceIn(menu, menuPoint, { ms: SURFACE_MENU_IN_MS });
      setTimeout(() => {
        document.addEventListener('mousedown', onMenuDocDown, true);
        document.addEventListener('keydown', onMenuKey, true);
      }, 0);
    };

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
      return async () => {
        await new Promise((r) => setTimeout(r, wipeDurationMs()));
        removalsInFlight = Math.max(0, removalsInFlight - 1);
        render();
        list.style.minHeight = '';
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
    const confirmOpen = (name, newTab = false) => app.confirm(
      newTab
        ? `Open "${shortName(name || 'Untitled')}" in a new tab?`
        : `Open "${shortName(name || 'Untitled')}" here? Any unsaved changes in the current tab will be replaced.`,
      { title: 'Open project', confirmLabel: 'Open', confirmIcon: 'folder', cancelLabel: 'Cancel' });

    const makeRow = (meta, opts = {}) => {
      const row = document.createElement('div');
      row.className = 'project-row';
      if (!opts.temp && meta && meta.id != null) row.dataset.id = meta.id;
      if (opts.temp) row.classList.add('project-temp');
      if (opts.incognito) row.classList.add('project-incognito');
      if (!opts.temp && meta.id === app.activeProjectId) row.classList.add('project-active');
      // A local project linked to a server project gets the golden remote outline —
      // it IS that server project (opened/saved), shown once with its real thumbnail.
      const serverLinked = !opts.temp && meta && meta.remoteId && meta.address;
      if (serverLinked) row.classList.add('project-remote');
      // A project imported from a portable .stencil file (and not server-linked) gets a
      // bronze outline — distinct from a plain local project and the golden server rows.
      const fileLinked = !opts.temp && meta && meta.fromFile && !serverLinked;
      if (fileLinked) row.classList.add('project-file');

      // Multi-select checkbox (saved rows only; the synthetic temp/incognito row has none).
      if (!opts.temp) {
        const key = localKey(meta.id);
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.className = 'project-select';
        cb.checked = selected.has(key);
        if (cb.checked) row.classList.add('project-selected');
        selectables.set(key, { kind: 'local', id: meta.id, serverUrl: meta.address || null, isServer: isServerMeta(meta), meta });
        cb.addEventListener('click', e => e.stopPropagation());   // don't open the row
        cb.addEventListener('change', () => {
          toggleSelect(key, { kind: 'local', id: meta.id, serverUrl: meta.address || null, isServer: isServerMeta(meta), meta }, cb.checked);
          row.classList.toggle('project-selected', cb.checked);
        });
        row.appendChild(cb);
      }

      const thumbWrap = document.createElement('div');
      thumbWrap.className = 'project-thumb';
      if (meta && meta.thumbnail) {
        const img = document.createElement('img');
        img.src = meta.thumbnail;
        img.alt = '';
        thumbWrap.appendChild(img);
      } else {
        thumbWrap.innerHTML = icon(opts.incognito ? 'incognito' : (opts.temp ? 'pencil' : 'image'), { size: 24 });
        thumbWrap.classList.add('project-thumb-placeholder');
      }
      row.appendChild(thumbWrap);
      enableThumbZoom(thumbWrap);

      const info = document.createElement('div');
      info.className = 'project-info';
      const name = document.createElement('div');
      name.className = 'project-name';
      name.textContent = opts.incognito ? 'Incognito (unsaved)' : (opts.temp ? 'Temporary (unsaved)' : (meta.name || 'Untitled'));
      // A saved project's custom colour overrides the default grey, but KEEPS the theme-flipped
      // shadow (from .project-name CSS) so even a light custom colour stays legible on a light
      // theme. Empty → CSS keeps the fixed grey + the same shadow.
      if (!opts.temp && meta.color) name.style.color = meta.color;
      // Tooltip on the TEXT column, not the row: over the thumbnail the magnified image is
      // the preview, and a native tooltip up the thumb's ancestor chain would cover it.
      if (!opts.temp && meta) { const tip = projectTooltip(meta); if (tip) info.title = tip; }
      info.appendChild(name);

      // Inline rename. The name's dblclick stops propagation, so the ROW's dblclick never
      // fires — but its two clicks did arm the deferred open, which would pop the
      // confirmation modal over the rename input a moment later. Cancel it.
      let rowGesture = null;
      const beginRename = () => {
        if (opts.temp) return;
        rowGesture?.cancel();
        const wrap = document.createElement('span');
        wrap.className = 'project-rename-wrap';
        const input = document.createElement('input');
        input.className = 'project-name-edit';
        input.type = 'text';
        input.value = meta.name || 'Untitled';
        input.title = 'Project name';
        const accept = document.createElement('button');
        accept.type = 'button';
        accept.className = 'name-edit-btn name-edit-accept';
        accept.innerHTML = icon('check', { size: 14 });
        accept.title = 'Save name (Enter)';
        const cancel = document.createElement('button');
        cancel.type = 'button';
        cancel.className = 'name-edit-btn name-edit-cancel';
        cancel.innerHTML = icon('x', { size: 14 });
        cancel.title = 'Cancel (Esc)';
        wrap.append(input, accept, cancel);
        name.replaceWith(wrap);
        // ✓/✗ FORM from dust (desktop revealControls parity); their hover already
        // draws the check / strikes the cross (animations.css .ic-check/.ic-x).
        markIn(accept);
        markIn(cancel);
        input.focus();
        input.select();
        let done = false;
        const finish = (save, next) => {
          if (done) return;
          done = true;
          // …and come apart BEFORE the re-render sweeps the editor away — the
          // clouds are copies on <body>, so the rebuild never waits for them.
          markOut(accept);
          markOut(cancel);
          // renameProject re-checks uniqueness; adopt the name only if accepted.
          if (save && next && next !== meta.name && app.renameProject(meta.id, next)) meta.name = next;
          render();
        };
        // Live-validated ✓/✗ (always shown here): ✓ enabled only for a changed, valid
        // name, its tooltip explaining any rejection. Enter = ✓, Escape/click-away = ✗.
        wireNameEditor(input, accept, cancel, {
          alwaysShow: true,
          current: () => meta.name || '',
          validate: (v) => app.storage.store.validateName(v, meta.id),
          commit: (v) => finish(true, v),
          cancel: () => finish(false),
        });
        input.addEventListener('keydown', e => e.stopPropagation());   // keep modal hotkeys out
        input.addEventListener('blur', () => finish(false));           // click-away discards
        input.addEventListener('click', e => e.stopPropagation());
      };
      if (!opts.temp) name.addEventListener('dblclick', e => { e.stopPropagation(); beginRename(); });

      const sub = document.createElement('div');
      sub.className = 'project-sub';
      if (opts.temp) {
        sub.textContent = opts.incognito ? 'Current tab · incognito · never saved' : 'Current tab · not saved to storage';
      } else {
        const bits = [];
        if (meta.createdAt) bits.push(`Created ${fmtDate(meta.createdAt)}`);
        bits.push(fmtDate(meta.updatedAt));
        const exp = expiryLabel(meta);
        if (exp.text) bits.push(exp.text);
        sub.textContent = bits.join(' · ');
        if (exp.expired) sub.classList.add('project-expired');
        else if (exp.soon) sub.classList.add('project-expiring');
        // The worker echoes every tab's active id (including ours), so exclude
        // this tab's own active project — only mark it when a DIFFERENT tab has it.
        if (isPeerOpen(meta)) {
          const open = document.createElement('span');
          open.className = 'project-open-elsewhere';
          open.innerHTML = `${icon('external', { size: 12 })}<span>opened in another tab</span>`;
          sub.appendChild(open);
        }
        // Origin badge — one per row, with an icon + tooltip naming where the project lives:
        // golden server, bronze .stencil file, or (default) a browser-storage globe. Incognito
        // rows are never persisted, so they get none.
        if (serverLinked) {
          const badge = document.createElement('span');
          badge.className = 'project-remote-badge';
          badge.title = `Shared server project — ${meta.address}`;
          badge.innerHTML = `${icon('server', { size: 12 })}<span>${escapeHtml(meta.address)}</span>`;
          sub.appendChild(badge);
        } else if (fileLinked) {
          const badge = document.createElement('span');
          badge.className = 'project-file-badge';
          badge.title = 'Opened from a .stencil project file';
          badge.innerHTML = `${icon('file-text', { size: 12 })}<span>.stencil</span>`;
          sub.appendChild(badge);
        } else if (!opts.incognito) {
          const badge = document.createElement('span');
          badge.className = 'project-local-badge';
          badge.title = 'Stored in this browser';
          badge.innerHTML = `${icon('globe', { size: 12 })}<span>browser</span>`;
          sub.appendChild(badge);
        }
        // The row for whatever's open in THIS editor right now — right after the origin
        // badge (browser/server/.stencil), not a separate mark of its own, and no icon:
        // just the word, in the accent that already means "this one" everywhere else.
        if (meta.id === app.activeProjectId) {
          const cur = document.createElement('span');
          cur.className = 'project-current-badge';
          cur.title = 'Currently open in this editor';
          cur.textContent = ' (Current)';
          sub.appendChild(cur);
        }
      }
      info.appendChild(sub);
      // Free-text description line (when set): a single truncated line under the metadata.
      if (!opts.temp && meta.description) {
        const desc = document.createElement('div');
        desc.className = 'project-desc';
        desc.textContent = meta.description;
        info.appendChild(desc);
      }
      row.appendChild(info);

      if (!opts.temp) {
        const isActive = meta.id === app.activeProjectId;
        // True while THIS project is open in a DIFFERENT tab — removing/moving it
        // would yank it out from under that tab, so both are blocked then.
        const openElsewhere = () => isPeerOpen(meta);

        const moveToServer = async () => {
          if (openElsewhere()) { notify('Open in another tab — close it there first', 'fail'); return; }
          const urls = app.connections.urls;
          let address = urls[0];
          if (urls.length > 1) {
            address = await app.choose(
              `Move "${shortName(meta.name || 'Untitled')}" to which server? It becomes a server-backed project.`,
              { title: 'Move to server', confirmLabel: 'Move', confirmIcon: 'upload', options: urls.map(u => ({ value: u, label: u })) });
            if (!address) return;
          } else if (!(await app.confirm(
            `Move "${shortName(meta.name || 'Untitled')}" to server ${address}? It becomes a server-backed project.`,
            { title: 'Move to server', confirmLabel: 'Move', confirmIcon: 'upload' }))) {
            return;
          }
          try { await app.moveProjectToServer(meta.id, address); notify('Moved to server', 'ok'); render(); scrollRowIntoView(meta.id); }
          catch (err) { notify(`Could not move to server — ${err.message}`, 'fail'); }
        };
        const copyToServer = async () => {
          const address = await pickServer(`Copy "${shortName(meta.name || 'Untitled')}" to which server?`);
          if (!address) return;
          const name = await app.prompt('Name for the server copy:', { title: 'Copy to server', confirmLabel: 'Copy', confirmIcon: 'copy', defaultValue: `${meta.name || 'Untitled'}-copy` });
          if (name == null) return;
          try { await app.copyProjectToServer(meta.id, address, { name }); notify('Copied to server', 'ok'); render(); }
          catch (err) { notify(`Could not copy to server — ${err.message}`, 'fail'); }
        };
        const removeRow = async () => {
          if (openElsewhere()) { notify('Open in another tab — close it there first', 'fail'); return; }
          const note = serverLinked
            ? `Remove the local copy of "${shortName(meta.name || 'Untitled')}"? It stays on the server ${meta.address}.`
            : `Remove project "${shortName(meta.name || 'Untitled')}"? This cannot be undone.`;
          if (!(await app.confirm(note, { title: 'Remove project', danger: true, confirmIcon: 'trash' }))) return;
          // The row collapses away first; render() then rebuilds the list without it.
          const settle = beginRemoval();
          await leaveThenRemove(rowById(meta.id), () => {}, scatterGridFor(1));
          app.removeProject(meta.id);
          await settle();
        };

        // Opening the row per the gesture's intent (rowOpenIntent): `here` switches this
        // tab, `newtab` spawns one, `confirm` gates behind the shared modal. Clicking the
        // already-active project just closes the modal.
        const openWithIntent = async ({ confirm = true, target = 'here' } = {}) => {
          if (target === 'newtab') {
            if (confirm && !(await confirmOpen(meta.name, true))) return;
            app.openProjectInNewTab(meta.id);   // the same path the ⋯ menu uses
            return;
          }
          if (isActive) { close(); return; }
          if (confirm && !(await confirmOpen(meta.name))) return;
          app.switchToProject(meta.id);
          close();
        };
        const open = () => openWithIntent({ confirm: true, target: 'here' });

        // Per-row colour: a throwaway native colour picker paints the project name. A second
        // "Clear colour" item (shown only when a colour is set) resets it to the theme accent.
        const pickColor = () => {
          const picker = document.createElement('input');
          picker.type = 'color';
          picker.value = meta.color || '#7c3aed';
          picker.style.cssText = 'position:fixed;left:-9999px;width:1px;height:1px;opacity:0;';
          document.body.appendChild(picker);
          const apply = () => { app.setProjectColor(meta.id, picker.value); meta.color = picker.value; render(); };
          picker.addEventListener('change', () => { apply(); picker.remove(); });
          try {
            if (typeof picker.showPicker === 'function') picker.showPicker();
            else picker.click();
          } catch {
            picker.click();
          }
        };
        const clearColor = () => { app.setProjectColor(meta.id, ''); meta.color = ''; render(); };

        // Edit the project's search keywords via a prompt (comma/space separated). The store
        // normalizes; a server-linked project also pushes them to the server.
        const editKeywords = async () => {
          const cur = (meta.keywords || []).join(' ');
          const v = await app.prompt('Keywords (comma or space separated):', { title: 'Project keywords', confirmLabel: 'Save', confirmIcon: 'save', defaultValue: cur });
          if (v == null) return;
          const updated = app.setProjectKeywords(meta.id, v.split(/[\s,]+/));
          if (updated) meta.keywords = updated.keywords;
          render();
        };

        // Edit the project's free-text description via a prompt. The store trims + stores it;
        // an empty value clears it. Mirrors editKeywords / the colour picker above.
        const editDescription = async () => {
          const cur = meta.description || '';
          const v = await app.prompt('Description:', { title: 'Project description', confirmLabel: 'Save', confirmIcon: 'save', defaultValue: cur });
          if (v == null) return;
          const updated = store.setDescription(meta.id, v);
          if (updated) meta.description = updated.description;
          render();
        };

        // One menu definition, shared by the "⋯" button and a right-click on the row.
        const menuItems = () => [
          isActive ? null : { icon: 'folder', label: 'Open', onClick: open },
          { icon: 'external', label: 'Open in new tab', onClick: async () => { if (await confirmOpen(meta.name, true)) app.openProjectInNewTab(meta.id); } },
          { icon: 'pencil', label: 'Rename', onClick: () => beginRename() },
          { icon: 'palette', label: 'Set color…', onClick: pickColor },
          meta.color ? { icon: 'x', label: 'Clear color', onClick: clearColor } : null,
          { icon: 'flag', label: 'Keywords…', onClick: editKeywords },
          { icon: 'file-text', label: 'Description…', onClick: editDescription },
          { icon: 'calendar', label: 'Expiration…', onClick: () => document.querySelector('stencil-expiration-modal')?.openFor(meta.id) },
          (hasServers() && !serverLinked) ? { icon: 'server', label: 'Move to server', onClick: moveToServer } : null,
          (hasServers() && !serverLinked) ? { icon: 'copy', label: 'Copy to server', onClick: copyToServer } : null,
          { icon: 'trash', label: 'Remove', danger: true, onClick: removeRow },
        ];

        const actions = document.createElement('div');
        actions.className = 'project-actions';
        const menuBtn = document.createElement('button');
        menuBtn.className = 'project-more btn-icon';
        menuBtn.title = 'More actions';
        menuBtn.innerHTML = icon('more', { size: 15 });
        menuBtn.addEventListener('click', e => {
          e.stopPropagation();
          showMenu(menuBtn, menuItems());
        });
        actions.appendChild(menuBtn);
        row.appendChild(actions);

        // Right-click (and, on touch, the long-press callout) opens the same overflow
        // menu at the cursor — that menu is where "Open in new tab" lives for fingers.
        row.addEventListener('contextmenu', e => {
          e.preventDefault();
          showMenu(menuBtn, menuItems(), { x: e.clientX, y: e.clientY });
        });

        row.classList.add('project-clickable');
        // ── Open gestures (see rowOpenIntent): click / dblclick / ⌘-variants on a
        // mouse, tap / long press on touch, Enter or Space from the keyboard. ──
        const gesture = createOpenGesture({ run: (intent) => openWithIntent(intent) });
        rowGesture = gesture;      // the rename editor cancels any pending open
        row._openGesture = gesture;   // …and so do BOTH drag engines (see attachRowDrag)
        row.addEventListener('click', (e) => gesture.click(e));
        row.addEventListener('dblclick', (e) => gesture.dblclick(e));
        // MOVEMENT WINS (see pressMove): past the slop the pending open is dropped and
        // the drop's click swallowed — the hold belongs to touchDrag's reorder pickup.
        row.addEventListener('pointerdown', (e) => {
          if (e.target.closest('input,button,select,.project-name-edit')) return;
          gesture.pressStart({ x: e.clientX, y: e.clientY });
        });
        row.addEventListener('pointermove', (e) => gesture.pressMove({ x: e.clientX, y: e.clientY }));
        for (const type of ['pointerup', 'pointercancel', 'pointerleave']) {
          row.addEventListener(type, () => gesture.pressEnd());
        }
        // A real drag pickup (mouse HTML5 DnD; the touch engine reports its own below)
        // kills any pending open outright.
        row.addEventListener('dragstart', () => gesture.dragStart());
        // Keyboard: the row is a real button. Enter/Space = a plain click (confirmed),
        // ⌘/Ctrl held targets a new tab — the same mapping as the mouse.
        row.tabIndex = 0;
        row.setAttribute('role', 'button');
        row.addEventListener('keydown', (e) => {
          if (e.key !== 'Enter' && e.key !== ' ') return;
          e.preventDefault();
          gesture.key(e);
        });
      } else if (opts.incognito && hasServers()) {
        // The incognito session has no menu, but it CAN be published to a server (it then
        // becomes a normal server-backed project and leaves incognito).
        const saveToServer = async () => {
          const urls = app.connections.urls;
          let address = urls[0];
          if (urls.length > 1) {
            address = await app.choose('Save this incognito project to which server?',
              { title: 'Save to server', confirmLabel: 'Save', confirmIcon: 'upload', options: urls.map(u => ({ value: u, label: u })) });
            if (!address) return;
          }
          try { await app.publishIncognitoToServer(address); render(); }
          catch (err) { notify(`Could not save to server — ${err.message}`, 'fail'); }
        };
        const actions = document.createElement('div');
        actions.className = 'project-actions';
        const btn = document.createElement('button');
        btn.className = 'project-more btn-icon';
        btn.title = 'Save to server';
        btn.innerHTML = icon('server', { size: 15 });
        btn.addEventListener('click', e => { e.stopPropagation(); saveToServer(); });
        actions.appendChild(btn);
        row.appendChild(actions);
      }
      if (opts.temp) {
        // The synthetic "Current tab" row is whatever's already open here — there's nothing
        // to switch to, so a click just closes the modal (inner buttons stop-propagate).
        row.classList.add('project-clickable');
        row.addEventListener('click', close);
      }
      return row;
    };

    // Build a row for a server-stored project: golden outline + a server badge.
    // "Open" fetches the original image bytes + layout from the server and loads
    // them into the editor (read into a local editing session).
    const makeRemoteRow = (meta) => {
      const row = document.createElement('div');
      row.className = 'project-row project-remote';
      if (meta && meta.id != null) row.dataset.id = meta.id;
      // Multi-select checkbox (server projects are the move/copy-to-local batch targets).
      {
        const key = remoteKey(meta);
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.className = 'project-select';
        cb.checked = selected.has(key);
        if (cb.checked) row.classList.add('project-selected');
        selectables.set(key, { kind: 'remote', id: meta.id, serverUrl: meta.serverUrl, isServer: true, meta });
        cb.addEventListener('click', e => e.stopPropagation());
        cb.addEventListener('change', () => {
          toggleSelect(key, { kind: 'remote', id: meta.id, serverUrl: meta.serverUrl, isServer: true, meta }, cb.checked);
          row.classList.toggle('project-selected', cb.checked);
        });
        row.appendChild(cb);
      }
      const thumb = document.createElement('div');
      thumb.className = 'project-thumb project-thumb-placeholder';
      thumb.innerHTML = icon('server', { size: 24 });
      row.appendChild(thumb);
      enableThumbZoom(thumb);
      // Swap the server glyph for the real picture: prefer the server's stored bytes,
      // else load the `source` URL directly (an <img> needs no CORS); glyph stays if nothing loads.
      const showThumb = (src, revoke) => {
        const img = document.createElement('img');
        img.alt = '';
        img.src = src;
        // Keep blob URLs alive for the hover-magnify zoom (which reuses img.src); they're
        // revoked at the NEXT render instead of on load, so the preview isn't a broken image.
        if (revoke) remoteObjectUrls.add(src);
        thumb.innerHTML = '';
        thumb.classList.remove('project-thumb-placeholder');
        thumb.appendChild(img);
      };
      const sourceUrl = /^https?:/i.test(meta.source || '') ? meta.source : '';
      const conn = app.connections && app.connections.get(meta.serverUrl);
      if (conn) {
        remoteThumbBlob(conn, meta).then((blob) => {
          if (blob) showThumb(URL.createObjectURL(blob), true);
          else if (sourceUrl) showThumb(sourceUrl, false);
        });
      } else if (sourceUrl) {
        showThumb(sourceUrl, false);
      }

      const info = document.createElement('div');
      info.className = 'project-info';
      const name = document.createElement('div');
      name.className = 'project-name';
      name.textContent = meta.name || 'Untitled';
      // Server projects carry `color` in their ProjectRecord — paint the name with it.
      if (meta.color) name.style.color = meta.color;
      // Same informative hover tooltip as local rows (dimensions/orientation + description).
      { const tip = projectTooltip(meta); if (tip) name.title = tip; }
      const sub = document.createElement('div');
      sub.className = 'project-sub';
      // Server projects carry createdAt in their ProjectRecord — show it (they have
      // no local expiry). Shown before the server badge.
      if (meta.createdAt) {
        const created = document.createElement('span');
        created.className = 'project-created';
        created.textContent = `Created ${fmtDate(meta.createdAt)} · `;
        sub.appendChild(created);
      }
      // Server projects may carry an expiresAt (epoch ms; 0/absent = keep forever) —
      // shown next to the created date when the server has set one.
      if (meta.expiresAt) {
        const expires = document.createElement('span');
        expires.className = 'project-expires';
        expires.textContent = `Expires ${fmtDate(meta.expiresAt)} · `;
        sub.appendChild(expires);
      }
      const badge = document.createElement('span');
      badge.className = 'project-remote-badge';
      badge.innerHTML = `${icon('server', { size: 12 })}<span>${escapeHtml(meta.serverUrl)}</span>`;
      sub.appendChild(badge);
      info.append(name, sub);
      row.appendChild(info);

      const actions = document.createElement('div');
      actions.className = 'project-actions';

      // The row opens the server project on click (fetches image + layout). A brief
      // dimmed state reads as "working" since opening hits the network.
      let opening = false;
      const openFromServer = async () => {
        if (opening) return;
        if (!(await confirmOpen(meta.name))) return;
        opening = true;
        row.classList.add('is-opening');
        try { await openRemote(meta); close(); }
        catch (err) {
          notify(`Could not open server project — ${err.message}`, 'fail');
          row.classList.remove('is-opening');
          opening = false;
        }
      };

      const moveToLocal = async () => {
        if (!(await app.confirm(
          `Move "${shortName(meta.name || 'Untitled')}" to local storage? It will be removed from the server.`,
          { title: 'Move to local', confirmLabel: 'Move', confirmIcon: 'download' }))) return;
        try { const newId = await app.moveProjectToLocal(meta); notify('Moved to local', 'ok'); render(); scrollRowIntoView(newId); }
        catch (err) { notify(`Could not move to local — ${err.message}`, 'fail'); }
      };
      // Detached local copy (prompts a name, default "<name>-copy"), leaving the server copy
      // in place; opens the new local project.
      const copyToLocal = async () => {
        const name = await app.prompt('Name for the local copy:', { title: 'Copy to local', confirmLabel: 'Copy', confirmIcon: 'copy', defaultValue: `${meta.name || 'Untitled'}-copy` });
        if (name == null) return;
        try {
          const newId = await app.copyServerProjectToLocal(meta, { name });
          notify('Local copy created', 'ok');
          app.switchToProject(newId);
          close();
        } catch (err) { notify(`Could not make a local copy — ${err.message}`, 'fail'); }
      };
      // Incognito copy (no saving): load the project's content as an incognito session, in
      // this tab or a new one.
      const copyToIncognito = async () => {
        const where = await app.choose(`Open an incognito copy of "${shortName(meta.name || 'Untitled')}" where?`,
          { title: 'Incognito copy', confirmLabel: 'Open', confirmIcon: 'incognito', options: [
            { value: 'here', label: 'This tab (replace current)' },
            { value: 'newtab', label: 'New tab' },
          ] });
        if (!where) return;
        try { await app.copyServerProjectToIncognito(meta, { newTab: where === 'newtab' }); if (where === 'here') close(); }
        catch (err) { notify(`Could not open an incognito copy — ${err.message}`, 'fail'); }
      };
      const deleteFromServer = async () => {
        if (!(await app.confirm(`Delete server project "${shortName(meta.name || 'Untitled')}"? This cannot be undone.`, { title: 'Delete server project', danger: true, confirmIcon: 'trash' }))) return;
        const conn = app.connections && app.connections.get(meta.serverUrl);
        if (!conn) { notify('Not connected to that server', 'fail'); return; }
        try { const settle = beginRemoval(); await leaveThenRemove(rowById(meta.id), () => {}, scatterGridFor(1)); await conn.deleteProject(meta.id); invalidateRemotes(); await settle(); }
        catch (err) { notify(`Could not delete — ${err.message}`, 'fail'); }
      };

      // Secondary actions behind the "⋯" overflow menu (matches the local rows);
      // shared with the row's right-click context menu.
      const menuItems = () => [
        { icon: 'folder', label: 'Open from server', onClick: openFromServer },
        { icon: 'copy', label: 'Copy to local…', onClick: copyToLocal },
        { icon: 'incognito', label: 'Copy to incognito…', onClick: copyToIncognito },
        { icon: 'download', label: 'Move to local', onClick: moveToLocal },
        { icon: 'trash', label: 'Delete from server', danger: true, onClick: deleteFromServer },
      ];
      const menuBtn = document.createElement('button');
      menuBtn.className = 'project-more btn-icon';
      menuBtn.title = 'More actions';
      menuBtn.innerHTML = icon('more', { size: 15 });
      menuBtn.addEventListener('click', e => {
        e.stopPropagation();
        showMenu(menuBtn, menuItems());
      });

      actions.append(menuBtn);
      row.appendChild(actions);
      // Right-click anywhere on the row opens the same overflow menu at the cursor.
      row.addEventListener('contextmenu', e => {
        e.preventDefault();
        showMenu(menuBtn, menuItems(), { x: e.clientX, y: e.clientY });
      });
      row.classList.add('project-clickable');
      row.addEventListener('click', openFromServer);
      return row;
    };

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
      dragging: dragActive,
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

    // ── Per-session manual drag order ──
    // A drop rewrites the persisted key order and switches the sort to 'manual'. The order is
    // seeded from the full current ordering (ignoring the search filter) so every project keeps
    // a slot even when a drag happens while filtered; unknown/added ids fall to the end.
    let dragKey = null;
    let didReorder = false;
    let dragActive = false;
    let didZone = false;   // a drag-out zone action ran on the accepted drop (skip dragend render)
    // Row key -> { meta, isRemote } for the current render, so a drop on a drag-out zone can
    // resolve the dragged project without re-parsing the key (server urls contain ':').
    const keyMeta = new Map();
    const clearRowDropCues = () => list.querySelectorAll('.project-drop-before,.project-drop-after')
      .forEach((el) => el.classList.remove('project-drop-before', 'project-drop-after'));
    const persistManualDrop = (draggedKey, targetKey, before) => {
      const full = sortItems(buildItems({ applySearch: false }), sortMode === 'manual' ? 'name' : sortMode).map((i) => i.key);
      const base = sortMode === 'manual' ? loadOrder() : [];
      saveOrder(reconcileManualOrder(full, base, draggedKey, targetKey, before));
      setSortMode('manual');
    };

    // ── Drag-out drop zones ──
    // Overlay around the dialog while a row is dragged: top 70% splits Open here / Open in
    // a new tab, bottom 30% is Remove. Zones are PURELY VISUAL (pointer-events:none) — the
    // action is decided from the pointer's RELEASE position (zoneForPoint). Every zone confirms.
    let zonesEl = null;
    let lastX = 0;
    let lastY = 0;
    const buildZones = () => {
      const wrap = document.createElement('div');
      wrap.className = 'project-dropzones';
      wrap.innerHTML =
        '<div class="pdz pdz-here" data-action="here"><div class="pdz-label">' + icon('folder', { size: 22 }) + '<span>Open here</span></div></div>'
        + '<div class="pdz pdz-newtab" data-action="newtab"><div class="pdz-label">' + icon('external', { size: 22 }) + '<span>Open in a new tab</span></div></div>'
        + '<div class="pdz pdz-remove" data-action="remove"><div class="pdz-label">' + icon('trash', { size: 22 }) + '<span>Remove</span></div></div>';
      return wrap;
    };
    // Painted over the modal (inserted as the overlay's first child), shown only while dragging.
    const ensureZones = () => { if (!zonesEl) { zonesEl = buildZones(); overlay.insertBefore(zonesEl, overlay.firstChild); } return zonesEl; };
    const showZones = () => ensureZones().classList.add('is-dragging');
    const hideZones = () => { if (zonesEl) { zonesEl.classList.remove('is-dragging'); zonesEl.querySelectorAll('.pdz-over').forEach((z) => z.classList.remove('pdz-over')); } };
    // The zone the point falls in, or null when it's OVER the dialog card (reorder / no-op there).
    // Mirrors the visual bands: bottom 30% of the viewport = remove, else top split left/right.
    const zoneForPoint = (x, y) => {
      const card = overlay.querySelector('.app-modal');
      const r = card && card.getBoundingClientRect();
      if (r && pointInRect(x, y, r)) return null;  // over the dialog
      if (y > window.innerHeight * 0.7) return 'remove';
      return x < window.innerWidth / 2 ? 'here' : 'newtab';
    };
    const highlightZone = (zone) => {
      if (!zonesEl) return;
      for (const z of zonesEl.querySelectorAll('.pdz')) z.classList.toggle('pdz-over', z.dataset.action === zone);
    };
    // Track the pointer + highlight the live zone during a row drag. preventDefault over a zone so
    // the cursor reads as droppable and the drop is ACCEPTED — that suppresses the browser's
    // snap-back-to-source animation (the glitch where the row appeared to return to the list).
    const onDocDragOver = (e) => {
      if (!dragActive) return;
      lastX = e.clientX; lastY = e.clientY;
      const zone = zoneForPoint(lastX, lastY);
      highlightZone(zone);
      // dropEffect MUST stay compatible with effectAllowed ('move', set in dragstart): a
      // 'copy' effect makes the browser REJECT the drop (no drop event fires → snap-back,
      // no action). Keep every zone on 'move'.
      if (zone) { e.preventDefault(); try { e.dataTransfer.dropEffect = 'move'; } catch { /* noop */ } }
    };
    document.addEventListener('dragover', onDocDragOver);
    // Run the zone action on the accepted DROP (not dragend), so there's no snap-back glitch and
    // the action fires immediately. A reorder (drop on a row, stopPropagation) never reaches here.
    const onDocDrop = (e) => {
      if (!dragActive) return;
      const zone = zoneForPoint(e.clientX, e.clientY);
      if (!zone) return;   // over the dialog → row drop / nothing handles it
      e.preventDefault();
      didZone = true;
      performZoneAction(dragKey, zone);
    };
    document.addEventListener('drop', onDocDrop);
    const endDrag = () => {
      dragActive = false; dragKey = null; didReorder = false; didZone = false;
      hideZones(); clearRowDropCues();
      list.querySelectorAll('.project-dragging').forEach((el) => el.classList.remove('project-dragging'));
    };

    // Run the drag-out action for the dropped row (resolved via keyMeta), mirroring the ⋯-menu
    // equivalents so both paths behave identically.
    const performZoneAction = async (key, action) => {
      const info = keyMeta.get(key);
      if (!info) { render(); return; }
      const meta = info.meta;
      if (!info.isRemote) {
        const id = meta.id;
        if (action === 'here') { if (await confirmOpen(meta.name)) { app.switchToProject(id); close(); } else render(); }
        else if (action === 'newtab') { if (await confirmOpen(meta.name, true)) app.openProjectInNewTab(id); render(); }
        else if (action === 'remove') {
          const serverLinked = meta.remoteId && meta.address;
          const note = serverLinked
            ? `Remove the local copy of "${shortName(meta.name || 'Untitled')}"? It stays on the server ${meta.address}.`
            : `Remove project "${shortName(meta.name || 'Untitled')}"? This cannot be undone.`;
          if (!(await app.confirm(note, { title: 'Remove project', danger: true, confirmLabel: 'Yes', confirmIcon: 'trash', cancelLabel: 'No' }))) { render(); return; }
          const settle = beginRemoval();
          await leaveThenRemove(rowById(id), () => {}, scatterGridFor(1));
          app.removeProject(id);
          await settle();
        }
        return;
      }
      // Server (remote) row.
      if (action === 'here') { if (!(await confirmOpen(meta.name))) { render(); return; } try { await openRemote(meta); close(); } catch (err) { notify(`Could not open server project — ${err.message}`, 'fail'); render(); } }
      else if (action === 'newtab') { if (await confirmOpen(meta.name, true)) app.openRemoteProjectInNewTab(meta); render(); }
      else if (action === 'remove') {
        if (!(await app.confirm(`Delete server project "${shortName(meta.name || 'Untitled')}"? This cannot be undone.`, { title: 'Delete server project', danger: true, confirmLabel: 'Yes', confirmIcon: 'trash', cancelLabel: 'No' }))) { render(); return; }
        const conn = app.connections && app.connections.get(meta.serverUrl);
        if (!conn) { notify('Not connected to that server', 'fail'); return; }
        try { const settle = beginRemoval(); await leaveThenRemove(rowById(meta.id), () => {}, scatterGridFor(1)); await conn.deleteProject(meta.id); invalidateRemotes(); await settle(); }
        catch (err) { notify(`Could not delete — ${err.message}`, 'fail'); }
      }
    };

    const attachRowDrag = (row, key) => {
      row.draggable = true;
      row.dataset.dragKey = key;   // lets the touch path hit-test the drop target via elementFromPoint
      row.addEventListener('dragstart', (e) => {
        // Don't hijack clicks on interactive children (checkbox, ⋯ menu, rename input).
        if (e.target.closest('input,button,select,.project-name-edit')) { e.preventDefault(); return; }
        dragKey = key; didReorder = false; dragActive = true;
        row.classList.add('project-dragging');
        setTranslucentDragImage(e, row);  // translucent cursor-following ghost
        showZones();
        // Mark this as an internal reorder drag so the image-drop overlay ignores it.
        try { e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('application/x-stencil-reorder', 'project'); } catch { /* older DnD */ }
      });
      row.addEventListener('dragover', (e) => {
        if (!dragKey || dragKey === key) return;
        e.preventDefault();
        try { e.dataTransfer.dropEffect = 'move'; } catch { /* noop */ }
        const r = row.getBoundingClientRect();
        clearRowDropCues();
        row.classList.add(e.clientY < r.top + r.height / 2 ? 'project-drop-before' : 'project-drop-after');
      });
      row.addEventListener('dragleave', () => row.classList.remove('project-drop-before', 'project-drop-after'));
      row.addEventListener('drop', (e) => {
        if (!dragKey || dragKey === key) return;
        e.preventDefault(); e.stopPropagation();
        const r = row.getBoundingClientRect();
        persistManualDrop(dragKey, key, e.clientY < r.top + r.height / 2);
        didReorder = true;
      });
      row.addEventListener('dragend', () => {
        const acted = didZone;   // the zone action already ran on the accepted drop
        endDrag();               // resets flags + hides zones (the source row may be detached)
        if (!acted) render();    // reflect a reorder, or clean up after a no-op release
      });

      // Touch/pen: HTML5 DnD never fires on touch, so drive the SAME reorder + zone logic through
      // the pointer engine (long-press to pick up; swipe to scroll). Mouse ignores this path.
      makeTouchDraggable(row, {
        canStart: (e) => !e.target.closest('input,button,select,.project-name-edit'),
        // The pickup is a DRAG, never an open: drop any pending click intent (the engine
        // also swallows the click after a real drag — this covers the pickup itself).
        onStart: () => { row._openGesture?.dragStart(); dragKey = key; didReorder = false; didZone = false; dragActive = true; row.classList.add('project-dragging'); showZones(); },
        onMove: (x, y) => {
          lastX = x; lastY = y;
          const zone = zoneForPoint(x, y);
          highlightZone(zone);
          clearRowDropCues();
          if (!zone) {
            const target = document.elementFromPoint(x, y)?.closest('.project-row');
            if (target && target.dataset.dragKey && target.dataset.dragKey !== dragKey) {
              const r = target.getBoundingClientRect();
              target.classList.add(y < r.top + r.height / 2 ? 'project-drop-before' : 'project-drop-after');
            }
          }
        },
        onDrop: (x, y) => {
          const zone = zoneForPoint(x, y);
          if (zone) { didZone = true; performZoneAction(dragKey, zone); endDrag(); return; }
          const target = document.elementFromPoint(x, y)?.closest('.project-row');
          if (target && target.dataset.dragKey && target.dataset.dragKey !== dragKey) {
            const r = target.getBoundingClientRect();
            persistManualDrop(dragKey, target.dataset.dragKey, y < r.top + r.height / 2);
          }
          endDrag();
          render();
        },
        onCancel: () => { endDrag(); render(); },
      });
    };

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
      clearAllBtn.title = hasServers()
        ? 'Delete every local project (server projects are not affected)'
        : 'Delete every saved project';
      // …and only when there IS one. The synthetic "temporary (unsaved)" row is not a saved
      // project — with just that on screen, Clear All wiped nothing and the row came straight
      // back, which read as a removal that undid itself.
      const clearable = list.querySelectorAll('.project-row:not(.project-temp):not(.project-remote):not(.project-skeleton)').length;
      clearAllBtn.disabled = clearable === 0;
      clearAllBtn.dataset.disabledReason = 'No saved projects to clear';

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

    // Delete a server project even when this tab's live connection object is gone
    // (dropped feed, listing served from cache): fall back to a direct authenticated
    // DELETE with the token saved for that server.
    const deleteRemoteProject = async (serverUrl, id) => {
      const conn = app.connections?.get(serverUrl);
      if (conn) { await conn.deleteProject(id); return; }
      const saved = loadSavedServers().find((s) => {
        try { return normalizeUrl(s.url) === normalizeUrl(serverUrl); } catch { return false; }
      });
      if (!saved) throw new Error(`not connected to ${serverUrl}`);
      const res = await fetch(`${normalizeUrl(serverUrl)}/projects/${encodeURIComponent(id)}`, {
        method: 'DELETE', headers: { Authorization: `Bearer ${saved.token}` },
      });
      if (!res.ok && res.status !== 404) {   // already-gone counts as removed
        let msg = `HTTP ${res.status}`;
        try { const body = await res.json(); if (body && body.message) msg = body.message; } catch { /* not JSON */ }
        throw new Error(msg);
      }
    };

    // ── Batch actions over the checked rows ──
    // Partial failure must be loud and specific: a row whose action failed comes back on
    // the settle render, so without the summary the batch reads as silently dropping it.
    const runBatch = async (fn, okMsg, failMsg, settle = null) => {
      let done = 0;
      const failures = [];
      for (const s of sel()) {
        try { await fn(s); done++; }
        catch (err) { failures.push({ name: shortName(s.meta?.name || 'Untitled'), message: err.message }); }
      }
      clearSelection();
      // `settle` is the hold the CALLER opened before the rows started leaving (it knows
      // when that was); batches with no removal just re-render.
      if (settle) await settle(); else render();
      if (!failures.length) { if (done) notify(`${okMsg} (${done})`, 'ok'); return; }
      const names = failures.slice(0, 3).map((f) => `"${f.name}"`).join(', ')
        + (failures.length > 3 ? ` +${failures.length - 3} more` : '');
      notify(done
        ? `${okMsg} ${done} of ${done + failures.length} — failed on ${names}: ${failures[0].message}`
        : `${failMsg} ${names} — ${failures[0].message}`, 'fail');
    };
    batchBtns.clear.addEventListener('click', () => { clearSelection(); render(); });
    // Select-all toggles over the CURRENT render's rows (the filtered view), so a
    // filtered "select all" never sweeps up projects the user cannot see.
    document.getElementById('projects-select-all')?.addEventListener('click', () => {
      if (allSelected()) selected.clear();
      else for (const [k, e] of selectables) selected.set(k, e);
      updateBatchBar();
      render();
    });
    batchBtns.remove.addEventListener('click', async () => {
      if (!selected.size) return;
      if (!(await app.confirm(`Remove ${selected.size} selected project(s)? Server projects are deleted from the server.`, { title: 'Remove projects', danger: true, confirmIcon: 'trash' }))) return;
      // Every selected row scatters at once, then the batch runs — one shared animation.
      // Budgeted: scatterGridFor coarsens each row's grain on a mass removal so the
      // TOTAL clone count stays bounded.
      const settle = beginRemoval();
      await Promise.all(sel().map((s, i) =>
        leaveThenRemove(rowById(s.id), () => {}, scatterGridFor(selected.size, i))));
      await runBatch(async (s) => {
        if (s.kind === 'remote') {
          await deleteRemoteProject(s.serverUrl, s.id);
          invalidateRemotes();
        } else { app.removeProject(s.id); }
      }, 'Removed', 'Could not remove', settle);
    });
    batchBtns.moveServer.addEventListener('click', async () => {
      const address = await pickServer('Move the selected projects to which server?');
      if (!address) return;
      await runBatch(s => app.moveProjectToServer(s.id, address), 'Moved to server', 'Could not move');
    });
    batchBtns.copyServer.addEventListener('click', async () => {
      const address = await pickServer('Copy the selected projects to which server?');
      if (!address) return;
      await runBatch(s => app.copyProjectToServer(s.id, address), 'Copied to server', 'Could not copy');
    });
    batchBtns.moveLocal.addEventListener('click', async () => {
      if (!selected.size) return;
      if (!(await app.confirm(`Move ${selected.size} server project(s) to local? They will be removed from the server.`, { title: 'Move to local', confirmLabel: 'Move', confirmIcon: 'download' }))) return;
      await runBatch(s => app.moveProjectToLocal(s.meta), 'Moved to local', 'Could not move');
    });
    batchBtns.copyLocal.addEventListener('click', async () => {
      await runBatch(s => app.copyServerProjectToLocal(s.meta), 'Copied to local', 'Could not copy');
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
      const doomed = [...list.querySelectorAll('.project-row:not(.project-temp)')];
      const settle = beginRemoval();
      await Promise.all(doomed.map((row, i) =>
        leaveThenRemove(row, () => {}, scatterGridFor(doomed.length, i))));
      app.clearAllProjects();
      await settle();
    });

    window.addEventListener('stencil:connections-changed', () => {
      // A connect/disconnect or live server project-event invalidates the cached listing
      // so the next render re-fetches it. Guard against a mid-drag or mid-removal
      // re-render destroying the dragged/leaving row (mayRefresh).
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
