import { StencilElement, hostTag, define, wireModalShell } from '../base.js';
import { notify, shortName } from '../../utils.js';
import { icon } from '../icons.js';
import { COPY_SCOPE_LABELS } from '../../core/project/copy/options.js';
import { copySuffixName } from '../../core/project/meta/projectNaming.js';

// "Make a copy": the one confirmation every entry point opens (desktop twin:
// dialogs/projects/copy/CopyProjectDialog.cpp). A server source copies onto its server unless
// "Make a local copy" is ticked; only a local copy can open incognito, and then it is never saved.
const row = (id, label, tip) => `
                <div class="vs-row vs-checks cp-row" id="${id}-row">
                    <label class="vs-inline-check" id="${id}-label" for="${id}" data-title="${tip}"><input type="checkbox" id="${id}"> ${label}</label>
                </div>`;

export class StencilCopyProjectModal extends StencilElement {
  #openFor;
  static inner() {
    const btn = (id, glyph, label) => `<button id="copy-project-${id}" class="btn-icon-text">${icon(glyph, { size: 14 })}<span>${label}</span></button>`;
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('duplicate', { size: 18 })} Make a copy</h2>
                <button class="app-modal-close btn-icon-text" id="copy-project-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <p class="confirm-message" id="copy-project-question"></p>
${row('copy-project-local', 'Make a local copy', '')}
${row('copy-project-incognito', 'Open in incognito', 'Open the copy without ever saving it.')}
            </div>
            <div class="settings-footer">
                <span class="footer-hint"></span>
                ${btn('cancel', 'x', 'Cancel')}
                ${btn('just', 'duplicate', 'Just copy')}
                ${btn('newtab', 'external', 'Open in new tab')}
                ${btn('open', 'folder', 'Open')}
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-copy-project-modal', 'id="copy-project-modal-overlay" class="app-modal-overlay"', StencilCopyProjectModal.inner()); }

  // `target`: { id } a stored row (null: the live editor) or { remote } a server-only row, plus
  // `what`; `onDone(newId, open)` lets the projects list follow the copy.
  openFor(target, anchors) { this.#openFor?.(target, anchors); }

  wire(app) {
    const el = (id) => this.$(`copy-project-${id}`);
    const question = el('question');
    const local = el('local');
    const incognito = el('incognito');
    const localRow = el('local-row');
    const justBtn = el('just');
    let target = null;

    // The source's name and server: a server-only row, a stored row, or the live editor.
    const sourceOf = ({ id = null, remote = null }) => {
      if (remote) return { name: remote.name || 'Untitled', server: remote.serverUrl };
      const meta = id != null ? app.storage.store.getMeta(id) : (app.activeProjectId != null ? app.storage.store.getMeta(app.activeProjectId) : null);
      const name = meta?.name || app.imageBaseName || 'Untitled';
      const server = id != null ? (meta?.remoteId ? meta.address : null) : (app.remoteLink?.address || null);
      return { name, server };
    };
    // Only a local copy may stay unsaved, and an unsaved copy exists only once it is opened.
    const sync = () => {
      const onServer = !!target?.server && !local.checked;
      incognito.disabled = onServer;
      if (onServer) incognito.checked = false;
      justBtn.disabled = incognito.checked;
    };
    local.addEventListener('change', sync);
    incognito.addEventListener('change', sync);

    const { open, close } = wireModalShell(this, null, el('close'), {
      onOpen: () => {
        const { name, server } = target;
        const scope = COPY_SCOPE_LABELS[target.what].toLowerCase();
        question.textContent = `Copy “${shortName(name)}” (${scope}) as “${copySuffixName(app.storage.store.list(), name)}”?`;
        localRow.style.display = server ? '' : 'none';
        el('local-label').dataset.title = server ? `Make it in this browser instead of on ${server}.` : '';
        local.checked = false;
        incognito.checked = false;
        sync();
      },
      onClose: () => { target = null; },
    });
    this.#openFor = ({ id = null, remote = null, what, onDone = null } = {}, { from = null, backTo = null } = {}) => {
      target = { id, remote, what, onDone, ...sourceOf({ id, remote }) };
      open(from, backTo, { stacked: true });
    };

    // A new tab is opened inside the click, before the copy is written, so no popup blocker eats it.
    const run = async (how) => {
      const t = target;
      if (!t) return;
      const call = { id: t.id, remote: t.remote, what: t.what, open: how, incognito: incognito.checked, local: local.checked };
      const win = how === 'newtab' ? window.open('', '_blank') : null;
      close();
      try {
        const newId = await app.projectTransfer.copyProject({ ...call, win });
        notify(call.incognito ? 'Opened an incognito copy' : 'Copy made', 'ok');
        t.onDone?.(newId, how);
      } catch (err) {
        win?.close();
        notify(`Could not make the copy — ${err?.message || err}`, 'fail');
      }
    };
    el('cancel').addEventListener('click', () => close());
    justBtn.addEventListener('click', () => run('none'));
    el('newtab').addEventListener('click', () => run('newtab'));
    el('open').addEventListener('click', () => run('here'));
    return { open, close };
  }
}
define('stencil-copy-project-modal', StencilCopyProjectModal);
