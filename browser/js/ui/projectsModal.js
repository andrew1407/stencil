import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches } from './base.js';
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
    // Prefer the edited `result`, fall back to the `original`, null if neither loads.
    p = conn.fetchFile(meta.id, 'result')
      .catch(() => conn.fetchFile(meta.id, 'original'))
      .catch(() => null);
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
                <button class="app-modal-close btn-icon-text" id="projects-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="modal-search-bar">
                <input type="text" id="projects-search" class="modal-search" placeholder="Search projects…">
            </div>
            <div class="settings-body" id="projects-list"><!-- filled by JS --></div>
            <div class="settings-footer">
                <span class="footer-hint">Projects auto-save · unopened projects expire after 7 days</span>
                <button id="projects-blank-image" class="btn-icon-text" title="Create a blank image to draw on">${icon('image')}<span>Blank image</span></button>
                <button id="projects-new-editor" class="btn-icon-text">${icon('plus-circle')}<span>New editor</span></button>
                <button id="projects-clear-all" class="danger btn-icon-text">${icon('trash')}<span>Clear All</span></button>
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
    const store = app.storage.store;

    let peers = []; // active project ids open in OTHER tabs
    const hasServers = () => !!app.connections?.urls?.length;

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
    const showMenu = (anchor, items) => {
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
      // Right-align under the button; flip above / clamp so it stays on-screen.
      const r = anchor.getBoundingClientRect();
      const mw = menu.offsetWidth;
      const mh = menu.offsetHeight;
      let x = r.right - mw;
      let y = r.bottom + 6;
      if (y + mh > window.innerHeight - 8) y = r.top - mh - 6;
      menu.style.left = `${Math.max(8, x)}px`;
      menu.style.top = `${Math.max(8, y)}px`;
      openMenu = menu;
      setTimeout(() => {
        document.addEventListener('mousedown', onMenuDocDown, true);
        document.addEventListener('keydown', onMenuKey, true);
      }, 0);
    };

    // Build a single row element for a project meta (or the temp synthetic row).
    const makeRow = (meta, opts = {}) => {
      const row = document.createElement('div');
      row.className = 'project-row';
      if (opts.temp) row.classList.add('project-temp');
      if (opts.incognito) row.classList.add('project-incognito');
      if (!opts.temp && meta.id === app.activeProjectId) row.classList.add('project-active');
      // A local project linked to a server project gets the golden remote outline —
      // it IS that server project (opened/saved), shown once with its real thumbnail.
      const serverLinked = !opts.temp && meta && meta.remoteId && meta.address;
      if (serverLinked) row.classList.add('project-remote');

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
        const accept = document.createElement('button');
        accept.type = 'button'; accept.className = 'name-edit-btn name-edit-accept'; accept.innerHTML = icon('check', { size: 14 });
        const cancel = document.createElement('button');
        cancel.type = 'button'; cancel.className = 'name-edit-btn name-edit-cancel'; cancel.innerHTML = icon('x', { size: 14 }); cancel.title = 'Cancel';
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
        const bits = [fmtDate(meta.updatedAt)];
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
          badge.innerHTML = `${icon('server', { size: 12 })}<span>${meta.address}</span>`;
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
              `Move "${meta.name || 'Untitled'}" to which server? The local copy will be removed.`,
              { title: 'Move to server', confirmLabel: 'Move', options: urls.map(u => ({ value: u, label: u })) });
            if (!address) return;
          } else if (!(await app.confirm(
            `Move "${meta.name || 'Untitled'}" to server ${address}? The local copy will be removed.`,
            { title: 'Move to server', confirmLabel: 'Move' }))) {
            return;
          }
          try { await app.moveProjectToServer(meta.id, address); notify('Moved to server', 'ok'); render(); }
          catch (err) { notify(`Could not move to server — ${err.message}`, 'fail'); }
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

        // The row opens the project on click; every other action lives behind "⋯".
        const open = () => { if (isActive) return; app.switchToProject(meta.id); close(); };

        const actions = document.createElement('div');
        actions.className = 'project-actions';
        const menuBtn = document.createElement('button');
        menuBtn.className = 'project-more btn-icon';
        menuBtn.title = 'More actions';
        menuBtn.innerHTML = icon('more', { size: 15 });
        menuBtn.addEventListener('click', e => {
          e.stopPropagation();
          showMenu(menuBtn, [
            isActive ? null : { icon: 'folder', label: 'Open', onClick: open },
            { icon: 'external', label: 'Open in new tab', onClick: () => app.openProjectInNewTab(meta.id) },
            { icon: 'pencil', label: 'Rename', onClick: () => beginRename() },
            { icon: 'refresh', label: 'Renew expiry', onClick: () => { app.renewProject(meta.id); render(); } },
            (hasServers() && !serverLinked) ? { icon: 'server', label: 'Move to server', onClick: moveToServer } : null,
            { icon: 'trash', label: 'Remove', danger: true, onClick: removeRow },
          ]);
        });
        actions.appendChild(menuBtn);
        row.appendChild(actions);

        if (!isActive) row.classList.add('project-clickable');
        row.addEventListener('click', open);
      }
      return row;
    };

    // Build a row for a server-stored project: golden outline + a server badge.
    // "Open" fetches the original image bytes + layout from the server and loads
    // them into the editor (read into a local editing session).
    const makeRemoteRow = (meta) => {
      const row = document.createElement('div');
      row.className = 'project-row project-remote';
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
        if (revoke) img.addEventListener('load', () => URL.revokeObjectURL(img.src), { once: true });
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
      const sub = document.createElement('div');
      sub.className = 'project-sub';
      const badge = document.createElement('span');
      badge.className = 'project-remote-badge';
      badge.innerHTML = `${icon('server', { size: 12 })}<span>${meta.serverUrl}</span>`;
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
        try { await app.moveProjectToLocal(meta); notify('Moved to local', 'ok'); render(); }
        catch (err) { notify(`Could not move to local — ${err.message}`, 'fail'); }
      };
      const deleteFromServer = async () => {
        if (!(await app.confirm(`Delete server project "${meta.name || 'Untitled'}"? This cannot be undone.`, { title: 'Delete server project', danger: true }))) return;
        const conn = app.connections && app.connections.get(meta.serverUrl);
        if (!conn) { notify('Not connected to that server', 'fail'); return; }
        try { await conn.deleteProject(meta.id); render(); }
        catch (err) { notify(`Could not delete — ${err.message}`, 'fail'); }
      };

      // Secondary actions behind the "⋯" overflow menu (matches the local rows).
      const menuBtn = document.createElement('button');
      menuBtn.className = 'project-more btn-icon';
      menuBtn.title = 'More actions';
      menuBtn.innerHTML = icon('more', { size: 15 });
      menuBtn.addEventListener('click', e => {
        e.stopPropagation();
        showMenu(menuBtn, [
          { icon: 'folder', label: 'Open from server', onClick: openFromServer },
          { icon: 'download', label: 'Move to local', onClick: moveToLocal },
          { icon: 'trash', label: 'Delete from server', danger: true, onClick: deleteFromServer },
        ]);
      });

      actions.append(menuBtn);
      row.appendChild(actions);
      row.classList.add('project-clickable');
      row.addEventListener('click', openFromServer);
      return row;
    };

    // Fetch a remote project's image + layout and load it into the editor.
    const openRemote = async (meta) => {
      // If a local project is already linked to this server project, just switch to
      // it — never create a duplicate local copy or re-download the image.
      const linked = store.list().find(m => m.remoteId === meta.id && m.address === meta.serverUrl);
      if (linked) { app.switchToProject(linked.id); return; }
      const conn = app.connections && app.connections.get(meta.serverUrl);
      if (!conn) throw new Error('not connected');
      const full = await conn.getProject(meta.id);
      // Prefer the server's stored original bytes; if it holds none, fetch the `source`
      // URL directly (cross-origin, so it needs CORS — which typical image hosts send).
      let blob = null;
      try { blob = await conn.fetchFile(meta.id, 'original'); }
      catch { blob = null; }
      const src = full.project?.source || '';
      if (!blob && /^https?:/i.test(src)) {
        const resp = await fetch(src, { mode: 'cors' });
        if (!resp.ok) throw new Error(`source image returned ${resp.status}`);
        blob = await resp.blob();
      }
      if (!blob) throw new Error('no image bytes on the server');
      const ext = (blob.type && blob.type.split('/')[1]) || 'png';
      const file = new File([blob], `${meta.name || 'image'}.${ext}`, { type: blob.type || 'image/png' });
      app.loadImageFromFile(file, {
        source: src,
        resource: full.project?.resource || '',
        address: meta.serverUrl,
        remoteId: meta.id,
        version: full.project?.version || 0,
        layout: full.layout,
      });
    };

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

    const render = () => {
      const q = search.value || '';
      hideZoom();
      closeMenu();   // a rebuilt list invalidates any open row menu
      list.innerHTML = '';

      // Synthetic current-tab temporary/incognito row (always for this tab).
      if (app.storage.temporary) {
        const label = app.storage.incognito ? 'incognito (unsaved)' : 'temporary (unsaved)';
        if (rowMatches(label, q)) list.appendChild(makeRow(null, { temp: true, incognito: app.storage.incognito }));
      }

      const projects = store.list().filter(m => rowMatches(m.name || '', q));
      for (const meta of projects) list.appendChild(makeRow(meta));

      // Server-linked local rows already represent their remote project; collect
      // their keys so renderRemote() doesn't append a duplicate golden row.
      const claimed = new Set(
        projects
          .filter(m => m.remoteId && m.address)
          .map(m => `${m.address}|${m.remoteId}`));

      // If servers are connected, show shimmering skeleton rows while their listing
      // is in flight (renderRemote replaces them) — never a misleading "no projects"
      // flash. With no servers, show the real empty message immediately.
      if (hasServers()) {
        list.appendChild(makeSkeletonRow());
        list.appendChild(makeSkeletonRow());
      } else if (list.children.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'info-empty';
        empty.textContent = q.trim() ? 'No matching projects.' : 'No saved projects yet.';
        list.appendChild(empty);
      }

      // Append server-stored projects (golden) once their lists resolve.
      renderRemote(q, claimed);
    };

    const { open, close } = wireModalShell(overlay, openBtn, closeBtn, {
      onOpen: () => { search.value = ''; render(); }
    });

    attachSearchFilter(search, render);

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
