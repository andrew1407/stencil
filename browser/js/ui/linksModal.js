import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';

// ── Component: source/resource links modal ──────────────────────
// Opened from the toolbar 🔗 button: view/edit the CURRENT image's provenance — its
// name, the source image/video URL, and the originating web page (resource). Edits are
// live (each field commits on change). Adding a NEW image by URL now lives in the
// unified Open dialog (openImageModal), so this modal only edits what's already loaded.
export class StencilLinksModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('link', { size: 18 })} Image links</h2>
                <button class="app-modal-close btn-icon-text" id="links-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <!-- Edit the current image's links. Shown only when an image is loaded. -->
                <div id="links-edit-section">
                    <div class="vs-section">Project</div>
                    <div class="vs-row vs-field"><label>Name</label><input type="text" id="links-name" placeholder="Untitled"></div>

                    <div class="vs-section">Links</div>
                    <div class="vs-row vs-field"><label title="The image/video's own URL">Source</label>
                        <span class="links-field">
                            <input type="text" id="links-source" placeholder="(empty — local upload)">
                            <button id="links-source-open" class="links-open btn-icon" title="Open source in a new tab">${icon('external', { size: 14 })}</button>
                            <button id="links-source-clear" class="links-clear danger btn-icon" title="Remove source link">${icon('x', { size: 14 })}</button>
                        </span>
                    </div>
                    <div class="vs-row vs-field"><label title="The web page the image was found on">Resource</label>
                        <span class="links-field">
                            <input type="text" id="links-resource" placeholder="(empty)">
                            <button id="links-resource-open" class="links-open btn-icon" title="Open resource page in a new tab">${icon('external', { size: 14 })}</button>
                            <button id="links-resource-clear" class="links-clear danger btn-icon" title="Remove resource link">${icon('x', { size: 14 })}</button>
                        </span>
                    </div>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint" id="links-foot-hint"></span>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-links-modal', 'id="links-modal-overlay" class="app-modal-overlay"', StencilLinksModal.inner()); }

  wire(app) {
    const $ = id => document.getElementById(id);
    const overlay = $('links-modal-overlay');
    const nameEl = $('links-name');
    const sourceEl = $('links-source');
    const resourceEl = $('links-resource');
    const footHint = $('links-foot-hint');

    // Re-read the current project's name/source/resource into the fields. Pulled out
    // of onOpen so it can also refresh LIVE while the modal is open (e.g. when the
    // console's stencil.current.source = … updates the active project).
    const syncLinkFields = () => {
      nameEl.value = (app.activeProjectId && app.storage.store.getMeta(app.activeProjectId)?.name) || app.imageBaseName || '';
      sourceEl.value = app.imageSource || '';
      resourceEl.value = app.imageResource || '';
    };

    wireModalShell(overlay, $('links-btn'), $('links-close'), {
      // The 🔗 button is disabled without an image (see drawingApp refreshActions), so
      // the modal only ever opens to edit a loaded image's links.
      onOpen: () => {
        footHint.textContent = 'Editing the current image’s links.';
        syncLinkFields();
      },
    });

    // Live-refresh the open modal on project-set changes — keeps link fields in sync
    // with on-the-fly source/resource edits. Uses the window event (fired by
    // TabsCoordinator.projectsChanged in THIS tab; onProjectsChanged only fires for
    // OTHER tabs), so same-tab console edits refresh too.
    window.addEventListener('stencil:registry-changed', () => {
      if (overlay.classList.contains('modal-open') && app.image) syncLinkFields();
    });

    // ── Current-project links: edit / open / remove ──
    const persist = () => { app.storage.save(); };

    const commitName = () => {
      const v = nameEl.value.trim();
      // renameProject keeps imageBaseName in lockstep for the active project.
      if (app.activeProjectId && v) app.renameProject(app.activeProjectId, v);
    };
    nameEl.addEventListener('change', commitName);

    const bindLinkField = (input, openBtn, clearBtn, key) => {
      input.addEventListener('change', () => {
        app[key] = input.value.trim() || null;
        persist();
      });
      openBtn.addEventListener('click', () => {
        const url = input.value.trim();
        if (!url) {
          notify('No link to open', 'fail');
          return;
        }
        window.open(url, '_blank', 'noopener');
      });
      clearBtn.addEventListener('click', () => {
        input.value = '';
        app[key] = null;
        persist();
        notify('Link removed', 'ok');
      });
    };
    bindLinkField(sourceEl, $('links-source-open'), $('links-source-clear'), 'imageSource');
    bindLinkField(resourceEl, $('links-resource-open'), $('links-resource-clear'), 'imageResource');
  }
}
define('stencil-links-modal', StencilLinksModal);
