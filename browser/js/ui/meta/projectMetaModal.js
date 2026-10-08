import { StencilElement, hostTag, define, wireModalShell } from '../base.js';
import { notify } from '../../utils.js';
import { icon } from '../icons.js';

// descriptionModal.js and keywordsModal.js are the same window with a different field:
// commit/discard, only Save writes. `field` supplies the body markup, an optional footer
// button of its own and its value port (projectMetaModal.d.ts MetaFieldPort).
export const createProjectMetaModal = ({
  name, title, glyph, hint, noun, addLabel, field, load, save,
}) => {
  const cls = class extends StencilElement {
    #openFor;
    static inner() {
      return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon(glyph, { size: 18 })} ${title}</h2>
                <button class="app-modal-close btn-icon-text" id="${name}-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body meta-body">${field.markup(name)}</div>
            <div class="settings-footer">
                <span class="footer-hint">${hint}</span>
                <span class="chat-settings-actions">
                    ${field.footer ? field.footer(name) : ''}
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

    // A projects-list row's own project, stacked over the list; `onSaved` gets the updated meta.
    openFor(id, anchors) { this.#openFor?.(id, anchors); }

    wire(app) {
      const $ = (id) => document.getElementById(id);
      let shell;
      let row = null;   // { id, onSaved } while opened from a row, else the active project
      const targetId = () => (row ? row.id : app.activeProjectId);
      const commit = () => {
        const id = targetId();
        if (id == null) { notify(`Save the project first to add ${addLabel}`, 'fail'); return; }
        const updated = save(app, id, port.get());
        if (updated == null) { notify(`Could not save the ${noun}`, 'fail'); return; }
        row?.onSaved?.(updated);
        shell.close();
      };
      const port = field.wire(name, commit);
      shell = wireModalShell($(`${name}-overlay`), $(`${name}-btn`), $(`${name}-close`), {
        onClose: () => { port.reset?.(); row = null; },
        // Reopening from what is stored is what makes every close a discard.
        onOpen: () => {
          const id = targetId();
          port.set(load(id != null ? app.storage.store.getMeta(id) : null));
          setTimeout(() => port.focus(), 0);
        },
      });
      this.#openFor = (id, { from = null, backTo = null, onSaved = null } = {}) => {
        row = { id, onSaved };
        shell.open(from, backTo, { stacked: true });
      };
      $(`${name}-save`).addEventListener('click', commit);
      $(`${name}-cancel`).addEventListener('click', () => shell.close());
    }
  };
  define(`stencil-${name}-modal`, cls);
  return cls;
};

// The description field: one text area filling the body. `commitsOn` decides which Enter
// saves rather than typing a newline.
export const metaTextField = ({ placeholder, rows, commitsOn }) => ({
  markup: (name) =>
    `<textarea id="${name}-text" class="confirm-prompt-input meta-text" rows="${rows}" placeholder="${placeholder}"></textarea>`,
  wire: (name, commit) => {
    const text = document.getElementById(`${name}-text`);
    text.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && commitsOn(e)) { e.preventDefault(); commit(); }
    });
    return {
      set: (value) => { text.value = value; },
      get: () => text.value,
      focus: () => text.focus(),
      // Closing discards an unsaved edit; onOpen reads the store again.
      reset: () => { text.value = ''; },
    };
  },
});
