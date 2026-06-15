import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches } from './base.js';
// ── Component: projects chooser / switcher modal ────────────────
// Lists saved projects (most-recently-edited first) plus a synthetic row for
// the current temporary editor, with thumbnails, dates, expiry badges, and an
// "open elsewhere" marker driven by the TabsCoordinator peers feed. Rows are
// built at runtime (the static #projects-list stays comment-only) so the markup
// tests' no-backtick / no-runtime-id assertions stay green.
export class StencilProjectsModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>🗂 Projects</h2>
                <button class="app-modal-close" id="projects-close">✕ Close</button>
            </div>
            <div class="modal-search-bar">
                <input type="text" id="projects-search" class="modal-search" placeholder="Search projects…">
            </div>
            <div class="settings-body" id="projects-list"><!-- filled by JS --></div>
            <div class="settings-footer">
                <span class="footer-hint">Projects auto-save · unopened projects expire after 7 days</span>
                <button id="projects-blank-image" title="Create a blank image to draw on">🖼 Blank image</button>
                <button id="projects-new-editor">➕ New editor</button>
                <button id="projects-clear-all" class="danger">🗑 Clear All</button>
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
      try { return new Date(ts).toLocaleString(); } catch { return ''; }
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
        thumbWrap.textContent = opts.incognito ? '🕶' : (opts.temp ? '✎' : '🖼');
        thumbWrap.classList.add('project-thumb-placeholder');
      }
      row.appendChild(thumbWrap);

      const info = document.createElement('div');
      info.className = 'project-info';
      const name = document.createElement('div');
      name.className = 'project-name';
      name.textContent = opts.incognito ? 'Incognito (unsaved)' : (opts.temp ? 'Temporary (unsaved)' : (meta.name || 'Untitled'));
      info.appendChild(name);

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
        switchBtn.textContent = meta.id === app.activeProjectId ? '✓ Current' : 'Open';
        switchBtn.disabled = meta.id === app.activeProjectId;
        switchBtn.addEventListener('click', e => {
          e.stopPropagation();
          app.switchToProject(meta.id);
          close();
        });
        const renewBtn = document.createElement('button');
        renewBtn.className = 'project-renew';
        renewBtn.title = 'Renew — reset the 7-day expiry to start from now';
        renewBtn.textContent = '🔄';
        renewBtn.addEventListener('click', e => {
          e.stopPropagation();
          app.renewProject(meta.id);
          render();
        });
        const removeBtn = document.createElement('button');
        removeBtn.className = 'project-remove danger';
        removeBtn.title = 'Remove project';
        removeBtn.textContent = '✕';
        removeBtn.addEventListener('click', e => {
          e.stopPropagation();
          if (!confirm(`Remove project "${meta.name || 'Untitled'}"? This cannot be undone.`)) return;
          app.removeProject(meta.id);
          render();
        });
        actions.appendChild(switchBtn);
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

    newEditorBtn.addEventListener('click', () => {
      if (!confirm('Discard current editor and start a new blank (unsaved) editor?')) return;
      app.newEditor();
      close();
    });
    clearAllBtn.addEventListener('click', () => {
      if (!confirm('Permanently delete ALL saved projects?')) return;
      app.clearAllProjects();
      render();
    });

    // Re-render when another tab changes the project set or peer activity.
    app.tabs.onProjectsChanged(() => { if (overlay.classList.contains('modal-open')) render(); });
    app.tabs.onPeers(ids => {
      peers = ids || [];
      if (overlay.classList.contains('modal-open')) render();
    });

    // On-open chooser: if this is the only tab AND there are saved projects,
    // offer the chooser. Otherwise stay in the (already-blank) temporary editor.
    app.tabs.whenReady().then(({ youAreOnly }) => {
      if (youAreOnly && store.list().length) open();
    });
  }
}
define('stencil-projects-modal', StencilProjectsModal);
