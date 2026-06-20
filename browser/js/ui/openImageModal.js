import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';

// ── Component: open-another-image modal ─────────────────────────
// Opened from the toolbar's #open-image-btn (only shown once an image is loaded).
// Pick a file + optionally mark it incognito, then choose to either replace the
// current editor ("Open here") or launch it in a fresh tab ("Open in new tab").
// The new-tab path rides the existing #stencil=<JSON> hand-off (applyExternalLaunch),
// which is also the only vehicle that works for incognito (never persisted).
export class StencilOpenImageModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('image', { size: 18 })} Open Another Image</h2>
                <button class="app-modal-close btn-icon-text" id="open-image-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="vs-section">Image file</div>
                <div class="vs-row"><label>Choose</label><input type="file" id="open-image-file" accept="image/*"></div>
                <div class="vs-row">
                    <label>Incognito</label>
                    <span class="oi-incognito">
                        <input type="checkbox" id="open-image-incognito">
                        <span class="footer-hint">Edit without saving — the image is never written to storage.</span>
                    </span>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint"></span>
                <button id="open-image-cancel" class="btn-icon-text">${icon('x', { size: 14 })}<span>Cancel</span></button>
                <button id="open-image-here" class="btn-icon-text" disabled>${icon('image', { size: 14 })}<span>Open here</span></button>
                <button id="open-image-newtab" class="btn-icon-text" disabled>${icon('external', { size: 14 })}<span>Open in new tab</span></button>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-open-image-modal', 'id="open-image-modal-overlay" class="app-modal-overlay"', StencilOpenImageModal.inner()); }

  wire(app) {
    const overlay = document.getElementById('open-image-modal-overlay');
    const closeBtn = document.getElementById('open-image-close');
    const cancelBtn = document.getElementById('open-image-cancel');
    const fileEl = document.getElementById('open-image-file');
    const incog = document.getElementById('open-image-incognito');
    const hereBtn = document.getElementById('open-image-here');
    const newTabBtn = document.getElementById('open-image-newtab');

    // The toolbar's "Open another image" button is this modal's open trigger.
    const { open, close } = wireModalShell(overlay, document.getElementById('open-image-btn'), closeBtn, {
      onOpen: () => {
        fileEl.value = '';
        incog.checked = false;
        hereBtn.disabled = true;
        newTabBtn.disabled = true;
      }
    });
    // Cancel is just another close path.
    cancelBtn.addEventListener('click', close);

    // Both actions are inert until a file is chosen.
    fileEl.addEventListener('change', () => {
      const has = fileEl.files && fileEl.files.length > 0;
      hereBtn.disabled = !has;
      newTabBtn.disabled = !has;
    });

    hereBtn.addEventListener('click', () => {
      const file = fileEl.files && fileEl.files[0];
      if (!file) return;
      app.openImageHere(file, incog.checked);
      close();
    });
    newTabBtn.addEventListener('click', () => {
      const file = fileEl.files && fileEl.files[0];
      if (!file) return;
      app.openImageNewTab(file, incog.checked);
      close();
    });
  }
}
define('stencil-open-image-modal', StencilOpenImageModal);
