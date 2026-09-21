import { icon } from '../../icons.js';
import { escapeHtml } from '../../base.js';
import { notify, shortName } from '../../../utils.js';
import { leaveThenRemove, rowLeaveDust, ITEM_DUST_MS } from '../../motion.js';

// Thumbnail blobs keyed `serverUrl|id|version`, so the many re-renders (search keystrokes,
// live events, peer pings) share one fetch per version. Twin: ProjectsDialog::remoteThumbs_.
const remoteThumbCache = new Map();
const remoteThumbBlob = (conn, meta) => {
  const id = `${meta.serverUrl}|${meta.id}`;
  const key = `${id}|${meta.version ?? ''}`;
  let p = remoteThumbCache.get(key);
  if (!p) {
    // Drop any stale-version entry for this project so we hold ~one blob per project.
    for (const k of remoteThumbCache.keys())
      if (k.startsWith(`${id}|`)) remoteThumbCache.delete(k);
    // Only files the record says exist, preferring the edited `result`. With neither, this
    // resolves null and makes NO request — no 404 spam for files the server hasn't got.
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

// One server-project row. Every collaborator the row needs comes in explicitly, so the
// modal keeps the list state and this file only knows how to build and act on a row.
export function createRemoteRow(deps) {
  const {
    app, close, render,
    remoteKey, selected, selectables, toggleSelect,
    enableThumbZoom, showMenu, remoteObjectUrls,
    projectTooltip, fmtDate, confirmOpen, openRemote,
    scrollRowIntoView, beginRemoval, retireKey, rowById, invalidateRemotes,
  } = deps;
    // Golden outline + a server badge. "Open" fetches the image bytes and layout from the
    // server into a local editing session.
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
        cb.setAttribute('aria-label', `Select ${meta.name || 'project'}`);
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
      { const tip = projectTooltip(meta); if (tip) name.dataset.title = tip; }
      const sub = document.createElement('div');
      sub.className = 'project-sub';
      // ProjectRecord's createdAt (server rows have no local expiry), before the badge.
      if (meta.createdAt) {
        const created = document.createElement('span');
        created.className = 'project-created';
        created.textContent = `Created ${fmtDate(meta.createdAt)} · `;
        sub.appendChild(created);
      }
      // expiresAt is epoch ms; 0 or absent means keep forever, so nothing is shown.
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
      // Detached local copy, leaving the server's in place; opens the new local project.
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
      // Incognito copy (never saved), in this tab or a new one.
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
        try {
          const settle = beginRemoval();
          const revive = retireKey(remoteKey(meta));
          await leaveThenRemove(rowById(meta.id), () => {}, rowLeaveDust(1, 0, ITEM_DUST_MS));
          await conn.deleteProject(meta.id); invalidateRemotes(); await settle();
          revive();
        }
        catch (err) { notify(`Could not delete — ${err.message}`, 'fail'); }
      };

      // The "⋯" overflow menu, shared with the row's right-click menu (as local rows).
      const menuItems = () => [
        { icon: 'folder', label: 'Open from server', onClick: openFromServer },
        { icon: 'copy', label: 'Copy to local', onClick: copyToLocal },
        { icon: 'incognito', label: 'Copy to incognito', onClick: copyToIncognito },
        { icon: 'download', label: 'Move to local', onClick: moveToLocal },
        { icon: 'trash', label: 'Delete from server', danger: true, onClick: deleteFromServer },
      ];
      const menuBtn = document.createElement('button');
      menuBtn.className = 'project-more btn-icon';
      menuBtn.dataset.title = 'More actions';
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
  return makeRemoteRow;
}
