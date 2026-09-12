import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';
import { subscribe, EVENTS } from '../bus/appBus.js';

// View/edit the current image's provenance (source URL, resource page); each field commits
// on change. Adding a new image by URL lives in openImageModal.
export class StencilLinksModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('link', { size: 18 })} Image links</h2>
                <button class="app-modal-close btn-icon-text" id="links-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <!-- Edit the current image's links. Shown only when an image is loaded. The
                     project's name is edited in the projects list / title, not here. -->
                <div id="links-edit-section">
                    <div class="vs-row vs-field"><label data-title="The image/video's own URL">Source</label>
                        <span class="links-field">
                            <input type="text" id="links-source" placeholder="(empty — local upload)">
                            <button id="links-source-open" class="links-open btn-icon" data-title="Open source in a new tab">${icon('external', { size: 14 })}</button>
                            <button id="links-source-clear" class="links-clear danger btn-icon" data-title="Remove source link">${icon('x', { size: 14 })}</button>
                        </span>
                    </div>
                    <div class="vs-row vs-field"><label data-title="The web page the image was found on">Resource</label>
                        <span class="links-field">
                            <input type="text" id="links-resource" placeholder="(empty)">
                            <button id="links-resource-open" class="links-open btn-icon" data-title="Open resource page in a new tab">${icon('external', { size: 14 })}</button>
                            <button id="links-resource-clear" class="links-clear danger btn-icon" data-title="Remove resource link">${icon('x', { size: 14 })}</button>
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
    const sourceEl = $('links-source');
    const resourceEl = $('links-resource');
    const footHint = $('links-foot-hint');

// Also refreshes live while open (the console's stencil.current.source = … path).
    const syncLinkFields = () => {
      sourceEl.value = app.imageSource || '';
      resourceEl.value = app.imageResource || '';
    };

    wireModalShell(overlay, $('links-btn'), $('links-close'), {
// The 🔗 button is disabled without an image (drawingApp refreshActions).
      onOpen: () => {
        footHint.textContent = 'Editing the current image’s links.';
        syncLinkFields();
      },
    });

// The window event fires for THIS tab too (onProjectsChanged only fires for other tabs).
    subscribe(EVENTS.registryChanged, () => {
      if (overlay.classList.contains('modal-open') && app.image) syncLinkFields();
    });

    const persist = () => { app.storage.save(); };

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
