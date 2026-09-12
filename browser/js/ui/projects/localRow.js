import { icon } from '../icons.js';
import { escapeHtml } from '../base.js';
import { attachRowActions, attachIncognitoActions } from './rowActions.js';

// One LOCAL project row — ported from projectsModal.js's makeRow. Twin of remoteRow.js:
// the deps bag crosses as thunks, because `render`/`close` are declared after the factory.
// `opts.temp` is the synthetic "Current tab" row (unsaved), `opts.incognito` its private form.
export const createLocalRow = ({
  app, close, render, localKey, selected, selectables, isServerMeta, toggleSelect,
  enableThumbZoom, projectTooltip, fmtDate, expiryLabel, isPeerOpen, hasServers,
  pickServer, confirmOpen, scrollRowIntoView, openColorPicker, beginRemoval, retireKey,
  rowById, showMenu,
}) => (meta, opts = {}) => {
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
    if (!opts.temp && meta) { const tip = projectTooltip(meta); if (tip) info.dataset.title = tip; }
    info.appendChild(name);

    // Inline rename (ui/projects/rowRename.js). The name's dblclick stops propagation, so
    // the ROW's dblclick never fires — but its two clicks did arm the deferred open, which
    // the editor cancels through the row's own gesture.
    let rowActions = null;
    if (!opts.temp) name.addEventListener('dblclick', e => { e.stopPropagation(); rowActions?.beginRename(); });

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
        badge.dataset.title = `Shared server project — ${meta.address}`;
        badge.innerHTML = `${icon('server', { size: 12 })}<span>${escapeHtml(meta.address)}</span>`;
        sub.appendChild(badge);
      } else if (fileLinked) {
        const badge = document.createElement('span');
        badge.className = 'project-file-badge';
        badge.dataset.title = 'Opened from a .stencil project file';
        badge.innerHTML = `${icon('file-text', { size: 12 })}<span>.stencil</span>`;
        sub.appendChild(badge);
      } else if (!opts.incognito) {
        const badge = document.createElement('span');
        badge.className = 'project-local-badge';
        badge.dataset.title = 'Stored in this browser';
        badge.innerHTML = `${icon('globe', { size: 12 })}<span>browser</span>`;
        sub.appendChild(badge);
      }
      // The row for whatever's open in THIS editor right now — right after the origin
      // badge (browser/server/.stencil), not a separate mark of its own, and no icon:
      // just the word, in the accent that already means "this one" everywhere else.
      if (meta.id === app.activeProjectId) {
        const cur = document.createElement('span');
        cur.className = 'project-current-badge';
        cur.dataset.title = 'Currently open in this editor';
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
      rowActions = attachRowActions({
        row, name, meta, app, close: () => close(), render: () => render(),
        serverLinked, hasServers, isPeerOpen, pickServer, confirmOpen,
        scrollRowIntoView, openColorPicker, beginRemoval, retireKey, localKey,
        rowById, showMenu,
      });
    } else if (opts.incognito && hasServers()) {
      attachIncognitoActions({ row, app, render: () => render() });
    }
    if (opts.temp) {
      // The synthetic "Current tab" row is whatever's already open here — there's nothing
      // to switch to, so a click just closes the modal (inner buttons stop-propagate).
      row.classList.add('project-clickable');
      row.addEventListener('click', close);
    }
    return row;
};
