import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches } from './base.js';
import { wireNameEditor } from '../utils.js';
import { icon } from './icons.js';
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

    // Build a single row element for a project meta (or the temp synthetic row).
    const makeRow = (meta, opts = {}) => {
      const row = document.createElement('div');
      row.className = 'project-row';
      if (opts.temp) row.classList.add('project-temp');
      if (opts.incognito) row.classList.add('project-incognito');
      if (!opts.temp && meta.id === app.activeProjectId) row.classList.add('project-active');

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
      }
      info.appendChild(sub);
      row.appendChild(info);

      if (!opts.temp) {
        const actions = document.createElement('div');
        actions.className = 'project-actions';
        const switchBtn = document.createElement('button');
        switchBtn.className = 'project-switch';
        switchBtn.innerHTML = meta.id === app.activeProjectId ? `${icon('check', { size: 13 })}<span>Current</span>` : '<span>Open</span>';
        if (meta.id === app.activeProjectId) switchBtn.classList.add('btn-icon-text');
        switchBtn.disabled = meta.id === app.activeProjectId;
        switchBtn.addEventListener('click', e => {
          e.stopPropagation();
          app.switchToProject(meta.id);
          close();
        });
        // Open in a new tab — leaves the current tab as-is, so it stays enabled
        // even for the active project (a second view of the same project).
        const newTabBtn = document.createElement('button');
        newTabBtn.className = 'project-newtab btn-icon';
        newTabBtn.title = 'Open in a new tab';
        newTabBtn.innerHTML = icon('external', { size: 15 });
        newTabBtn.addEventListener('click', e => {
          e.stopPropagation();
          app.openProjectInNewTab(meta.id);
        });
        const renameBtn = document.createElement('button');
        renameBtn.className = 'project-rename btn-icon';
        renameBtn.title = 'Rename project';
        renameBtn.innerHTML = icon('pencil', { size: 15 });
        renameBtn.addEventListener('click', e => {
          e.stopPropagation();
          beginRename();
        });
        const renewBtn = document.createElement('button');
        renewBtn.className = 'project-renew btn-icon';
        renewBtn.title = 'Renew — reset the 7-day expiry to start from now';
        renewBtn.innerHTML = icon('refresh', { size: 15 });
        renewBtn.addEventListener('click', e => {
          e.stopPropagation();
          app.renewProject(meta.id);
          render();
        });
        const removeBtn = document.createElement('button');
        removeBtn.className = 'project-remove danger btn-icon';
        removeBtn.title = 'Remove project';
        removeBtn.innerHTML = icon('trash', { size: 15 });
        removeBtn.addEventListener('click', async e => {
          e.stopPropagation();
          if (!(await app.confirm(`Remove project "${meta.name || 'Untitled'}"? This cannot be undone.`, { title: 'Remove project', danger: true }))) return;
          app.removeProject(meta.id);
          render();
        });
        actions.appendChild(switchBtn);
        actions.appendChild(newTabBtn);
        actions.appendChild(renameBtn);
        actions.appendChild(renewBtn);
        actions.appendChild(removeBtn);
        row.appendChild(actions);

        row.addEventListener('click', () => {
          if (meta.id === app.activeProjectId) return;
          app.switchToProject(meta.id);
          close();
        });
      }
      return row;
    };

    const render = () => {
      const q = search.value || '';
      list.innerHTML = '';

      // Synthetic current-tab temporary/incognito row (always for this tab).
      if (app.storage.temporary) {
        const label = app.storage.incognito ? 'incognito (unsaved)' : 'temporary (unsaved)';
        if (rowMatches(label, q)) list.appendChild(makeRow(null, { temp: true, incognito: app.storage.incognito }));
      }

      const projects = store.list().filter(m => rowMatches(m.name || '', q));
      for (const meta of projects) list.appendChild(makeRow(meta));

      if (list.children.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'info-empty';
        empty.textContent = q.trim() ? 'No matching projects.' : 'No saved projects yet.';
        list.appendChild(empty);
      }
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
