import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches, escapeHtml } from './base.js';
import { wireNameEditor, notify } from '../utils.js';
import { icon } from './icons.js';

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
    const batchBar = document.getElementById('projects-batch-bar');
    const batchCount = document.getElementById('projects-batch-count');
    const store = app.storage.store;

    let peers = []; // active project ids open in OTHER tabs
    let incognitoPeers = []; // incognito sessions open in OTHER tabs ({ peerId, name, updatedAt })
    let filterMode = 'all';
    const hasServers = () => !!app.connections?.urls?.length;
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
        const open = () => { if (!isActive) app.switchToProject(meta.id); close(); };

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

        // One menu definition, shared by the "⋯" button and a right-click on the row.
        const menuItems = () => [
          isActive ? null : { icon: 'folder', label: 'Open', onClick: open },
          { icon: 'external', label: 'Open in new tab', onClick: () => app.openProjectInNewTab(meta.id) },
          { icon: 'pencil', label: 'Rename', onClick: () => beginRename() },
          { icon: 'palette', label: 'Set colour…', onClick: pickColor },
          meta.color ? { icon: 'x', label: 'Clear colour', onClick: clearColor } : null,
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

    // Remote projects render asynchronously; a token guards against stale appends
    // when the user types / the set changes mid-fetch.
    let remoteToken = 0;
    const renderRemote = async (q, claimed = new Set()) => {
      const mgr = app.connections;
      if (!mgr || !mgr.urls.length) return;
      const myToken = ++remoteToken;
      let remotes = [];
      let failed = false;
      try { remotes = await mgr.remoteProjects(); } catch { failed = true; }
      if (myToken !== remoteToken || !overlay.classList.contains('modal-open')) return;
      // This listing is current — drop the skeletons and render the real rows.
      list.querySelectorAll('.project-skeleton').forEach(el => el.remove());
      const matching = failed ? [] : remotes.filter((m) =>
        rowMatches(m.name || '', q) && !claimed.has(`${m.serverUrl}|${m.id}`));
      for (const meta of matching) list.appendChild(makeRemoteRow(meta));
      // Nothing to show (after skeletons cleared) → an honest empty / error message.
      if (!list.querySelector('.project-row')) {
        const empty = document.createElement('div');
        empty.className = 'info-empty';
        empty.textContent = failed ? 'Could not reach server.'
          : (q.trim() ? 'No matching projects.' : 'No saved projects yet.');
        list.appendChild(empty);
      }
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
      const showLocal = filterMode === 'all' || filterMode === 'local';
      const showServer = filterMode === 'all' || filterMode === 'server';
      const showIncog = filterMode === 'all' || filterMode === 'incognito';

      // Synthetic current-tab temporary/incognito row. In the incognito filter only a real
      // incognito session qualifies (a plain temporary editor does not).
      if (showIncog && app.storage.temporary) {
        const label = app.storage.incognito ? 'incognito (unsaved)' : 'temporary (unsaved)';
        const qualifies = filterMode === 'incognito' ? app.storage.incognito : true;
        if (qualifies && rowMatches(label, q)) list.appendChild(makeRow(null, { temp: true, incognito: app.storage.incognito }));
      }
      // Incognito sessions open in OTHER tabs (read-only).
      if (showIncog) {
        for (const p of incognitoPeers)
          if (rowMatches(p.name || 'Incognito', q)) list.appendChild(makeIncognitoPeerRow(p));
      }

      // Saved projects: pure-local rows under Local/All; server-linked (golden) under Server/All.
      const all = store.list().filter(m => rowMatches(m.name || '', q));
      const localLinked = all.filter(m => isServerMeta(m));
      if (showLocal) for (const meta of all.filter(m => !isServerMeta(m))) list.appendChild(makeRow(meta));
      if (showServer) for (const meta of localLinked) list.appendChild(makeRow(meta));

      // Server-linked local rows already represent their remote project; collect their keys so
      // renderRemote() doesn't append a duplicate golden row.
      const claimed = new Set(localLinked.map(m => `${m.address}|${m.remoteId}`));

      // Server rows: skeletons while the listing loads (renderRemote replaces them). Only when
      // the filter shows servers and at least one is connected.
      if (showServer && hasServers()) {
        list.appendChild(makeSkeletonRow());
        list.appendChild(makeSkeletonRow());
        renderRemote(q, claimed);
      } else if (list.children.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'info-empty';
        empty.textContent = q.trim() ? 'No matching projects.' : emptyLabelFor(filterMode);
        list.appendChild(empty);
      }

      updateBatchBar();
    };

    const { open, close } = wireModalShell(overlay, openBtn, closeBtn, {
      onOpen: () => { search.value = ''; clearSelection(); render(); }
    });

    attachSearchFilter(search, render);
    filterEl.addEventListener('change', () => { filterMode = filterEl.value; render(); });

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
      if (!(await app.confirm('Permanently delete ALL saved projects?', { title: 'Delete all projects', danger: true }))) return;
      app.clearAllProjects();
      render();
    });

    // Re-render when servers connect/disconnect or push live project events, so
    // the golden remote rows stay current.
    window.addEventListener('stencil:connections-changed', () => {
      if (overlay.classList.contains('modal-open')) render();
    });

    // Re-render when another tab changes the project set or peer activity.
    app.tabs.onProjectsChanged(() => { if (overlay.classList.contains('modal-open')) render(); });
    app.tabs.onPeers(ids => {
      peers = ids || [];
      if (overlay.classList.contains('modal-open')) render();
    });
    // Incognito sessions open in OTHER tabs (for the "Incognito tabs" filter).
    app.tabs.onIncognitoPeers(list => {
      incognitoPeers = list || [];
      if (overlay.classList.contains('modal-open')) render();
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
