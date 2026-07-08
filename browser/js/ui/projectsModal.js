import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches, escapeHtml } from './base.js';
import { wireNameEditor, notify } from '../utils.js';
import { icon } from './icons.js';
import { SORT_MODES, sortProjectItems, reconcileManualOrder } from './projectSort.js';
import { setTranslucentDragImage } from './dragGhost.js';
import { makeTouchDraggable } from './touchDrag.js';

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
// Lists saved projects (most-recent first) + a synthetic row for the current temp
// editor, with thumbnails/dates/expiry badges and an "open elsewhere" marker from
// the TabsCoordinator peers feed. Rows built at runtime (static #projects-list
// stays comment-only) to keep the markup tests' assertions green.
export class StencilProjectsModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('layers', { size: 18 })} Projects</h2>
                <button class="app-modal-close btn-icon-text" id="projects-close" title="Close (Esc)">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="modal-search-bar">
                <input type="text" id="projects-search" class="modal-search" placeholder="Search projects…">
                <select id="projects-filter" class="modal-filter" title="Filter projects">
                    <option value="all">All</option>
                    <option value="local">Local</option>
                    <option value="server">Server</option>
                    <option value="incognito">Incognito tabs</option>
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
                    <button id="projects-batch-move-server" class="btn-icon-text" title="Move the selected local projects to a server">${icon('server', { size: 13 })}<span>Move to server</span></button>
                    <button id="projects-batch-copy-server" class="btn-icon-text" title="Copy the selected local projects to a server">${icon('copy', { size: 13 })}<span>Copy to server</span></button>
                    <button id="projects-batch-move-local" class="btn-icon-text" title="Move the selected server projects to local">${icon('download', { size: 13 })}<span>Move to local</span></button>
                    <button id="projects-batch-copy-local" class="btn-icon-text" title="Copy the selected server projects to local">${icon('copy', { size: 13 })}<span>Copy to local</span></button>
                    <button id="projects-batch-remove" class="danger btn-icon-text" title="Remove the selected projects">${icon('trash', { size: 13 })}<span>Remove</span></button>
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
    // What the search box matches, per the mode: names, keywords, or both ("common", default).
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

    // Cached server-project listing for this render cycle: null = not loaded, [] = loaded/empty.
    // Cached (instead of re-fetched every keystroke, as the old renderRemote did) so mixed/date
    // sort modes can interleave server + local rows from one snapshot; invalidated on
    // connection/server-project changes and on each modal open.
    let remoteCache = null;
    let remoteLoading = false;
    let remoteFailed = false;
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
      batchBar.style.display = selected.size ? '' : 'none';
      batchCount.textContent = `${selected.size} selected`;
      const local = onlyLocalMovable();
      const remote = onlyRemoteMovable();
      batchBtns.moveServer.disabled = !(local && hasServers());
      batchBtns.copyServer.disabled = !(local && hasServers());
      batchBtns.moveLocal.disabled = !remote;
      batchBtns.copyLocal.disabled = !remote;
    };
    const clearSelection = () => { selected.clear(); updateBatchBar(); };
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
      return app.choose(message, { title: 'Choose server', confirmLabel: 'OK', options: urls.map(u => ({ value: u, label: u })) });
    };

    const fmtDate = ts => {
      if (!ts) return '';
      try {
        return new Date(ts).toLocaleString();
      } catch {
        return '';
      }
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

    // Magnified hover preview: a fixed-position floating copy of a row's thumbnail
    // that follows the cursor, shown while hovering a thumb that holds a real image
    // (not the placeholder glyph). One reused element, shared by local + remote rows.
    let zoomEl = null;
    const ensureZoom = () => {
      if (zoomEl) return zoomEl;
      zoomEl = document.createElement('div');
      zoomEl.className = 'project-thumb-zoom';
      zoomEl.innerHTML = '<img alt="">';
      document.body.appendChild(zoomEl);
      return zoomEl;
    };
    const hideZoom = () => { if (zoomEl) zoomEl.style.display = 'none'; };
    const positionZoom = e => {
      if (!zoomEl) return;
      const pad = 18;
      const w = zoomEl.offsetWidth;
      const h = zoomEl.offsetHeight;
      // Prefer down-right of the cursor; flip/clamp so it never leaves the viewport.
      let x = e.clientX + pad;
      let y = e.clientY + pad;
      if (x + w > window.innerWidth - 8) x = e.clientX - pad - w;
      if (y + h > window.innerHeight - 8) y = window.innerHeight - 8 - h;
      zoomEl.style.left = `${Math.max(8, x)}px`;
      zoomEl.style.top = `${Math.max(8, y)}px`;
    };
    const enableThumbZoom = thumbEl => {
      thumbEl.addEventListener('mouseenter', e => {
        const img = thumbEl.querySelector('img');
        if (!img || !img.src) return;   // placeholder glyph — nothing to magnify
        const z = ensureZoom();
        z.querySelector('img').src = img.src;
        z.style.display = 'block';
        positionZoom(e);
      });
      thumbEl.addEventListener('mousemove', positionZoom);
      thumbEl.addEventListener('mouseleave', hideZoom);
    };

    // ── Per-row overflow ("⋯") menu ───────────────────────────────
    // A single floating menu reused by every row, so secondary actions (new tab,
    // rename, renew, move-to-server/local, remove) live behind one "⋯" button
    // instead of crowding the row. Closes on click-away, Escape, or re-render.
    let openMenu = null;
    const closeMenu = () => {
      if (!openMenu) return;
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
      setTimeout(() => {
        document.addEventListener('mousedown', onMenuDocDown, true);
        document.addEventListener('keydown', onMenuKey, true);
      }, 0);
    };

    // Build a single row element for a project meta (or the temp synthetic row).
    // After a move re-sorts the list, keep the moved item visible + focused so it doesn't
    // appear to "jump away". Rows carry data-id; scroll the matching one into view.
    const scrollRowIntoView = (id) => {
      if (id == null) return;
      requestAnimationFrame(() => {
        const el = list.querySelector(`[data-id="${id}"]`);
        if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest' });
      });
    };

    // Opening a project switches THIS tab's session (replacing any unsaved work) or spawns a new
    // tab — so every open path (row click, the ⋯ "Open", the server "Open from server", and the
    // drag-to-here / drag-to-new-tab zones) confirms first. Returns true to proceed. `newTab`
    // tunes the wording (a new tab leaves the current one untouched).
    const confirmOpen = (name, newTab = false) => app.confirm(
      newTab
        ? `Open "${name || 'Untitled'}" in a new tab?`
        : `Open "${name || 'Untitled'}" here? Any unsaved changes in the current tab will be replaced.`,
      { title: 'Open project', confirmLabel: 'Open', cancelLabel: 'Cancel' });

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

      // Multi-select checkbox (saved rows only; the synthetic temp/incognito row has none).
      if (!opts.temp) {
        const key = localKey(meta.id);
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.className = 'project-select';
        cb.checked = selected.has(key);
        if (cb.checked) row.classList.add('project-selected');
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
      info.appendChild(name);

      // Inline rename: swap the name div for an input (✎ button or double-click).
      // Commit on Enter/blur, cancel on Escape. Saved projects only.
      const beginRename = () => {
        if (opts.temp) return;
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
        input.focus();
        input.select();
        let done = false;
        const finish = (save, next) => {
          if (done) return;
          done = true;
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
        if (peers.includes(meta.id) && meta.id !== app.storage.activeId) {
          const open = document.createElement('span');
          open.className = 'project-open-elsewhere';
          open.textContent = ' · open in another tab';
          sub.appendChild(open);
        }
        // Golden server badge on a server-linked local row, so it reads as the same
        // shared project as its (now-deduped) remote row.
        if (serverLinked) {
          const badge = document.createElement('span');
          badge.className = 'project-remote-badge';
          badge.innerHTML = `${icon('server', { size: 12 })}<span>${escapeHtml(meta.address)}</span>`;
          sub.appendChild(badge);
        }
      }
      info.appendChild(sub);
      row.appendChild(info);

      if (!opts.temp) {
        const isActive = meta.id === app.activeProjectId;
        // True while THIS project is open in a DIFFERENT tab — removing/moving it
        // would yank it out from under that tab, so both are blocked then.
        const openElsewhere = () => peers.includes(meta.id) && meta.id !== app.storage.activeId;

        const moveToServer = async () => {
          if (openElsewhere()) { notify('Open in another tab — close it there first', 'fail'); return; }
          const urls = app.connections.urls;
          let address = urls[0];
          if (urls.length > 1) {
            address = await app.choose(
              `Move "${meta.name || 'Untitled'}" to which server? It becomes a server-backed project.`,
              { title: 'Move to server', confirmLabel: 'Move', options: urls.map(u => ({ value: u, label: u })) });
            if (!address) return;
          } else if (!(await app.confirm(
            `Move "${meta.name || 'Untitled'}" to server ${address}? It becomes a server-backed project.`,
            { title: 'Move to server', confirmLabel: 'Move' }))) {
            return;
          }
          try { await app.moveProjectToServer(meta.id, address); notify('Moved to server', 'ok'); render(); scrollRowIntoView(meta.id); }
          catch (err) { notify(`Could not move to server — ${err.message}`, 'fail'); }
        };
        const copyToServer = async () => {
          const address = await pickServer(`Copy "${meta.name || 'Untitled'}" to which server?`);
          if (!address) return;
          const name = await app.prompt('Name for the server copy:', { title: 'Copy to server', confirmLabel: 'Copy', defaultValue: `${meta.name || 'Untitled'}-copy` });
          if (name == null) return;
          try { await app.copyProjectToServer(meta.id, address, { name }); notify('Copied to server', 'ok'); render(); }
          catch (err) { notify(`Could not copy to server — ${err.message}`, 'fail'); }
        };
        const removeRow = async () => {
          if (openElsewhere()) { notify('Open in another tab — close it there first', 'fail'); return; }
          const note = serverLinked
            ? `Remove the local copy of "${meta.name || 'Untitled'}"? It stays on the server ${meta.address}.`
            : `Remove project "${meta.name || 'Untitled'}"? This cannot be undone.`;
          if (!(await app.confirm(note, { title: 'Remove project', danger: true }))) return;
          app.removeProject(meta.id);
          render();
        };

        // The row opens the project on click; clicking the already-active project just
        // closes the modal (it's already open in this tab). Other actions live behind "⋯".
        const open = async () => {
          if (isActive) { close(); return; }
          if (!(await confirmOpen(meta.name))) return;
          app.switchToProject(meta.id);
          close();
        };

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
          const v = await app.prompt('Keywords (comma or space separated):', { title: 'Project keywords', confirmLabel: 'Save', defaultValue: cur });
          if (v == null) return;
          const updated = app.setProjectKeywords(meta.id, v.split(/[\s,]+/));
          if (updated) meta.keywords = updated.keywords;
          render();
        };

        // One menu definition, shared by the "⋯" button and a right-click on the row.
        const menuItems = () => [
          isActive ? null : { icon: 'folder', label: 'Open', onClick: open },
          { icon: 'external', label: 'Open in new tab', onClick: async () => { if (await confirmOpen(meta.name, true)) app.openProjectInNewTab(meta.id); } },
          { icon: 'pencil', label: 'Rename', onClick: () => beginRename() },
          { icon: 'palette', label: 'Set colour…', onClick: pickColor },
          meta.color ? { icon: 'x', label: 'Clear colour', onClick: clearColor } : null,
          { icon: 'flag', label: 'Keywords…', onClick: editKeywords },
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

        // Right-click anywhere on the row opens the same overflow menu at the cursor.
        row.addEventListener('contextmenu', e => {
          e.preventDefault();
          showMenu(menuBtn, menuItems(), { x: e.clientX, y: e.clientY });
        });

        row.classList.add('project-clickable');
        row.addEventListener('click', open);
      } else if (opts.incognito && hasServers()) {
        // The incognito session has no menu, but it CAN be published to a server (it then
        // becomes a normal server-backed project and leaves incognito).
        const saveToServer = async () => {
          const urls = app.connections.urls;
          let address = urls[0];
          if (urls.length > 1) {
            address = await app.choose('Save this incognito project to which server?',
              { title: 'Save to server', confirmLabel: 'Save', options: urls.map(u => ({ value: u, label: u })) });
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
          `Move "${meta.name || 'Untitled'}" to local storage? It will be removed from the server.`,
          { title: 'Move to local', confirmLabel: 'Move' }))) return;
        try { const newId = await app.moveProjectToLocal(meta); notify('Moved to local', 'ok'); render(); scrollRowIntoView(newId); }
        catch (err) { notify(`Could not move to local — ${err.message}`, 'fail'); }
      };
      // Detached local copy (prompts a name, default "<name>-copy"), leaving the server copy
      // in place; opens the new local project.
      const copyToLocal = async () => {
        const name = await app.prompt('Name for the local copy:', { title: 'Copy to local', confirmLabel: 'Copy', defaultValue: `${meta.name || 'Untitled'}-copy` });
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
        const where = await app.choose(`Open an incognito copy of "${meta.name || 'Untitled'}" where?`,
          { title: 'Incognito copy', confirmLabel: 'Open', options: [
            { value: 'here', label: 'This tab (replace current)' },
            { value: 'newtab', label: 'New tab' },
          ] });
        if (!where) return;
        try { await app.copyServerProjectToIncognito(meta, { newTab: where === 'newtab' }); if (where === 'here') close(); }
        catch (err) { notify(`Could not open an incognito copy — ${err.message}`, 'fail'); }
      };
      const deleteFromServer = async () => {
        if (!(await app.confirm(`Delete server project "${meta.name || 'Untitled'}"? This cannot be undone.`, { title: 'Delete server project', danger: true }))) return;
        const conn = app.connections && app.connections.get(meta.serverUrl);
        if (!conn) { notify('Not connected to that server', 'fail'); return; }
        try { await conn.deleteProject(meta.id); render(); }
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

    // Remote listing fetch: a token guards against a stale in-flight fetch resolving after
    // the set changed. ensureRemotes() fills remoteCache once, then re-renders.
    let remoteToken = 0;
    const showsServer = () => filterMode === 'all' || filterMode === 'server';
    const ensureRemotes = () => {
      if (!showsServer() || !hasServers() || remoteCache !== null || remoteLoading) return;
      remoteLoading = true; remoteFailed = false;
      const myToken = ++remoteToken;
      app.connections.remoteProjects()
        .then((list_) => { if (myToken !== remoteToken) return; remoteCache = list_ || []; remoteLoading = false; if (overlay.classList.contains('modal-open')) render(); })
        .catch(() => { if (myToken !== remoteToken) return; remoteCache = []; remoteFailed = true; remoteLoading = false; if (overlay.classList.contains('modal-open')) render(); });
    };
    const invalidateRemotes = () => { remoteCache = null; remoteFailed = false; remoteToken++; };

    // ── Build one flat, sortable item list (local + cached server rows) ──
    // Each item carries a stable key, a lowercased name + a date for the comparators, an
    // isRemote flag, and a build() that returns the row element (reusing makeRow/makeRemoteRow).
    const localRowKey = (m) => `local:${m.id}`;
    const remoteRowKey = (m) => `remote:${m.serverUrl}:${m.id}`;
    const metaName = (m) => (m.name || '').toLowerCase();
    const metaDate = (m) => m.updatedAt || m.createdAt || 0;
    const buildItems = ({ applySearch }) => {
      const q = applySearch ? (search.value || '') : '';
      const showLocal = filterMode === 'all' || filterMode === 'local';
      const showServer = showsServer();
      const items = [];
      const all = store.list().filter((m) => !applySearch || matchRow(m.name, m.keywords, q));
      const localLinked = all.filter((m) => isServerMeta(m));
      if (showLocal) for (const meta of all.filter((m) => !isServerMeta(m)))
        items.push({ key: localRowKey(meta), name: metaName(meta), date: metaDate(meta), isRemote: false, meta, build: () => makeRow(meta) });
      if (showServer) for (const meta of localLinked)
        items.push({ key: localRowKey(meta), name: metaName(meta), date: metaDate(meta), isRemote: false, meta, build: () => makeRow(meta) });
      // Server (golden) rows from the cache, deduped against server-linked local rows.
      if (showServer && Array.isArray(remoteCache)) {
        const claimed = new Set(localLinked.map((m) => `${m.address}|${m.remoteId}`));
        for (const meta of remoteCache) {
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

    // ── Drag-out drop zones (feature #4) ──
    // A three-zone overlay shown around the dialog while a project row is dragged: the top 70%
    // splits into "Open here" (left) / "Open in a new tab" (right); the bottom 30% (full width) is
    // a red "Remove" zone. The zones are PURELY VISUAL (pointer-events:none) — the action is
    // decided from the pointer's RELEASE position (zoneForPoint), so dragging anywhere outside the
    // dialog reliably triggers it (a hidden hit-target behind the modal card was unreachable).
    // Remove confirms (Yes/No) like the ⋯-menu remove; open actions don't (like clicking Open).
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
      if (r && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return null;  // over the dialog
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
      // dropEffect MUST stay compatible with effectAllowed ('move', set in dragstart): a 'copy'
      // effect makes the browser REJECT the drop (no drop event fires → snap-back, no action). That
      // silently broke the open zones while Remove ('move') worked. Keep every zone on 'move'.
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
            ? `Remove the local copy of "${meta.name || 'Untitled'}"? It stays on the server ${meta.address}.`
            : `Remove project "${meta.name || 'Untitled'}"? This cannot be undone.`;
          if (!(await app.confirm(note, { title: 'Remove project', danger: true, confirmLabel: 'Yes', cancelLabel: 'No' }))) { render(); return; }
          app.removeProject(id);
          render();
        }
        return;
      }
      // Server (remote) row.
      if (action === 'here') { if (!(await confirmOpen(meta.name))) { render(); return; } try { await openRemote(meta); close(); } catch (err) { notify(`Could not open server project — ${err.message}`, 'fail'); render(); } }
      else if (action === 'newtab') { if (await confirmOpen(meta.name, true)) app.openRemoteProjectInNewTab(meta); render(); }
      else if (action === 'remove') {
        if (!(await app.confirm(`Delete server project "${meta.name || 'Untitled'}"? This cannot be undone.`, { title: 'Delete server project', danger: true, confirmLabel: 'Yes', cancelLabel: 'No' }))) { render(); return; }
        const conn = app.connections && app.connections.get(meta.serverUrl);
        if (!conn) { notify('Not connected to that server', 'fail'); return; }
        try { await conn.deleteProject(meta.id); invalidateRemotes(); render(); }
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
        onStart: () => { dragKey = key; didReorder = false; didZone = false; dragActive = true; row.classList.add('project-dragging'); showZones(); },
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
            : 'No saved projects yet.';

    const render = () => {
      const q = search.value || '';
      hideZoom();
      closeMenu();   // a rebuilt list invalidates any open row menu
      // Free the previous render's remote thumbnail blob URLs (kept alive for the hover-zoom).
      for (const u of remoteObjectUrls) URL.revokeObjectURL(u);
      remoteObjectUrls.clear();
      list.innerHTML = '';
      const showServer = showsServer();
      const showIncog = filterMode === 'all' || filterMode === 'incognito';

      // Synthetic current-tab temporary/incognito row (pinned at the top, above the sorted
      // rows). In the incognito filter only a real incognito session qualifies.
      if (showIncog && app.storage.temporary) {
        const label = app.storage.incognito ? 'incognito (unsaved)' : 'temporary (unsaved)';
        const qualifies = filterMode === 'incognito' ? app.storage.incognito : true;
        if (qualifies && rowMatches(label, q)) list.appendChild(makeRow(null, { temp: true, incognito: app.storage.incognito }));
      }
      // Incognito sessions open in OTHER tabs (read-only) — also pinned above the sorted rows.
      if (showIncog) {
        for (const p of incognitoPeers)
          if (rowMatches(p.name || 'Incognito', q)) list.appendChild(makeIncognitoPeerRow(p));
      }

      // Kick off (or reuse) the cached server listing, then render local + server rows as one
      // sorted, drag-reorderable list per the active sort mode.
      if (showServer && hasServers()) ensureRemotes();
      keyMeta.clear();
      for (const it of sortItems(buildItems({ applySearch: true }), sortMode)) {
        keyMeta.set(it.key, it);
        const row = it.build();
        attachRowDrag(row, it.key);
        list.appendChild(row);
      }

      // Server listing still loading → shimmer skeletons after the sorted rows.
      const loadingRemotes = showServer && hasServers() && remoteCache === null;
      if (loadingRemotes) { list.appendChild(makeSkeletonRow()); list.appendChild(makeSkeletonRow()); }

      // Nothing to show (and not still loading) → an honest empty / error message.
      if (!list.querySelector('.project-row:not(.project-skeleton)') && !loadingRemotes) {
        const empty = document.createElement('div');
        empty.className = 'info-empty';
        empty.textContent = remoteFailed ? 'Could not reach server.'
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

      updateBatchBar();
    };

    const { open, close } = wireModalShell(overlay, openBtn, closeBtn, {
      // Re-fetch the server listing on each open so a freshly-opened modal is current.
      onOpen: () => { search.value = ''; clearSelection(); invalidateRemotes(); sortEl.value = sortMode; searchModeEl.value = searchMode; render(); }
    });

    attachSearchFilter(search, render);
    filterEl.addEventListener('change', () => { filterMode = filterEl.value; render(); });
    sortEl.value = sortMode;
    sortEl.addEventListener('change', () => { setSortMode(sortEl.value); render(); });
    searchModeEl.value = searchMode;
    searchModeEl.addEventListener('change', () => { searchMode = searchModeEl.value; ssSet(SEARCH_MODE_KEY, searchMode); render(); });

    // ── Batch actions over the checked rows ──
    const runBatch = async (fn, okMsg, failMsg) => {
      let done = 0;
      for (const s of sel()) {
        try { await fn(s); done++; } catch (err) { notify(`${failMsg} — ${err.message}`, 'fail'); }
      }
      clearSelection();
      render();
      if (done) notify(`${okMsg} (${done})`, 'ok');
    };
    batchBtns.clear.addEventListener('click', () => { clearSelection(); render(); });
    batchBtns.remove.addEventListener('click', async () => {
      if (!selected.size) return;
      if (!(await app.confirm(`Remove ${selected.size} selected project(s)? Server projects are deleted from the server.`, { title: 'Remove projects', danger: true }))) return;
      await runBatch(async (s) => {
        if (s.kind === 'remote') {
          const conn = app.connections?.get(s.serverUrl);
          if (conn) await conn.deleteProject(s.id);
        } else { app.removeProject(s.id); }
      }, 'Removed', 'Could not remove');
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
      if (!(await app.confirm(`Move ${selected.size} server project(s) to local? They will be removed from the server.`, { title: 'Move to local', confirmLabel: 'Move' }))) return;
      await runBatch(s => app.moveProjectToLocal(s.meta), 'Moved to local', 'Could not move');
    });
    batchBtns.copyLocal.addEventListener('click', async () => {
      await runBatch(s => app.copyServerProjectToLocal(s.meta), 'Copied to local', 'Could not copy');
    });

    newEditorBtn.addEventListener('click', async () => {
      if (!(await app.confirm('Discard current editor and start a new blank (unsaved) editor?', { title: 'New editor' }))) return;
      app.newEditor();
      close();
    });
    clearAllBtn.addEventListener('click', async () => {
      const msg = hasServers()
        ? 'Are you sure? This permanently deletes ALL local projects. Server projects are not affected.'
        : 'Are you sure? This permanently deletes ALL saved projects.';
      const title = hasServers() ? 'Delete all local projects' : 'Delete all projects';
      if (!(await app.confirm(msg, { title, danger: true, confirmLabel: 'Yes', cancelLabel: 'No' }))) return;
      app.clearAllProjects();
      render();
    });

    // Re-render when servers connect/disconnect or push live project events, so
    // the golden remote rows stay current.
    window.addEventListener('stencil:connections-changed', () => {
      // A connection connect/disconnect or a live server project-event invalidates the
      // cached server listing so the next render re-fetches it. Guard against a mid-drag
      // re-render destroying the dragged row.
      invalidateRemotes();
      if (!dragActive && overlay.classList.contains('modal-open')) render();
    });

    // Re-render when another tab changes the project set or peer activity (never mid-drag).
    app.tabs.onProjectsChanged(() => { if (!dragActive && overlay.classList.contains('modal-open')) render(); });
    app.tabs.onPeers(ids => {
      peers = ids || [];
      if (!dragActive && overlay.classList.contains('modal-open')) render();
    });
    // Incognito sessions open in OTHER tabs (for the "Incognito tabs" filter).
    app.tabs.onIncognitoPeers(list => {
      incognitoPeers = list || [];
      if (!dragActive && overlay.classList.contains('modal-open')) render();
    });

    // On-open chooser: offer it only if this is the only tab AND saved projects
    // exist. Skipped when launched to open a specific project (?open=<id> deep link)
    // or image (extension #stencil= hand-off) — user already chose; don't pop over
    // it. Otherwise stay in the blank temporary editor.
    app.tabs.whenReady().then(({ youAreOnly }) => {
      if (youAreOnly && store.list().length && !app.pendingOpenProjectId && !app.hasExternalLaunch) open();
    });
  }
}
define('stencil-projects-modal', StencilProjectsModal);
