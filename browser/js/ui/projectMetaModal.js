import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';

// descriptionModal.js and keywordsModal.js are the same window with a different field:
// one textarea, commit/discard (only Save writes). Each spec supplies what differs.
export const createProjectMetaModal = ({
  name, title, glyph, rows, placeholder, hint, noun, addLabel, load, save, commitsOn,
}) => {
  const cls = class extends StencilElement {
    static inner() {
      return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon(glyph, { size: 18 })} ${title}</h2>
                <button class="app-modal-close btn-icon-text" id="${name}-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <textarea id="${name}-text" class="confirm-prompt-input" rows="${rows}" placeholder="${placeholder}"></textarea>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">${hint}</span>
                <span class="chat-settings-actions">
                    <button id="${name}-cancel" class="btn-icon-text">${icon('x', { size: 14 })}<span>Cancel</span></button>
                    <button id="${name}-save" class="btn-icon-text primary">${icon('check', { size: 14 })}<span>Save</span></button>
                </span>
            </div>
        </div>
    `;
    }
    static template() {
      return hostTag(`stencil-${name}-modal`, `id="${name}-overlay" class="app-modal-overlay"`, cls.inner());
    }

    wire(app) {
      const $ = (id) => document.getElementById(id);
      const text = $(`${name}-text`);
      const shell = wireModalShell($(`${name}-overlay`), $(`${name}-btn`), $(`${name}-close`), {
        // Reopening from what is stored is what makes every close a discard.
        onOpen: () => {
          const id = app.activeProjectId;
          text.value = load(id != null ? app.storage.store.getMeta(id) : null);
          setTimeout(() => text.focus(), 0);
        },
      });
      const commit = () => {
        const id = app.activeProjectId;
        if (id == null) { notify(`Save the project first to add ${addLabel}`, 'fail'); return; }
        if (save(app, id, text.value) == null) { notify(`Could not save the ${noun}`, 'fail'); return; }
        shell.close();
      };
      $(`${name}-save`).addEventListener('click', commit);
      $(`${name}-cancel`).addEventListener('click', () => shell.close());
      text.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && commitsOn(e)) { e.preventDefault(); commit(); }
      });
    }
  };
  define(`stencil-${name}-modal`, cls);
  return cls;
};
