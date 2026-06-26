import { StencilElement, hostTag, define, wireModalShell, fillTargetSelect } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';
import { isVideoFile, videoFileToImageFile } from '../core/videoFrame.js';

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
                <div class="vs-section">Image or video file</div>
                <div class="vs-row"><label>Choose</label><input type="file" id="open-image-file" accept="image/*,video/*"></div>
                <!-- Frame time: only shown when a video is chosen (a still frame is captured). -->
                <div class="vs-row" id="open-image-frame-row" style="display:none">
                    <label title="Capture the frame at this time (seconds)">Frame (s)</label>
                    <input type="number" id="open-image-frame" min="0" step="0.1" value="0" style="width:6rem">
                </div>
                <div class="vs-row">
                    <label>Incognito</label>
                    <span class="oi-incognito">
                        <input type="checkbox" id="open-image-incognito">
                        <span class="footer-hint">Edit without saving — the image is never written to storage.</span>
                    </span>
                </div>
                <!-- Save target: only shown when at least one server is connected. -->
                <div class="vs-row" id="open-image-target-row" style="display:none">
                    <label title="Open here locally or create on a connected server">Save to</label>
                    <select id="open-image-target"></select>
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
    const targetEl = document.getElementById('open-image-target');
    const targetRow = document.getElementById('open-image-target-row');
    const frameEl = document.getElementById('open-image-frame');
    const frameRow = document.getElementById('open-image-frame-row');

    // The toolbar's "Open another image" button is this modal's open trigger.
    const { open, close } = wireModalShell(overlay, document.getElementById('open-image-btn'), closeBtn, {
      onOpen: () => {
        fileEl.value = '';
        incog.checked = false;
        hereBtn.disabled = true;
        newTabBtn.disabled = true;
        frameRow.style.display = 'none';
        fillTargetSelect(targetEl, targetRow, app.connections);
      }
    });
    // Cancel is just another close path.
    cancelBtn.addEventListener('click', close);

    // Both actions are inert until a file is chosen; the frame-time row appears for video.
    fileEl.addEventListener('change', () => {
      const file = fileEl.files && fileEl.files[0];
      const has = !!file;
      hereBtn.disabled = !has;
      newTabBtn.disabled = !has;
      frameRow.style.display = has && isVideoFile(file) ? '' : 'none';
    });

    // A video file is converted to a captured still frame first (reusing the same
    // capture as the scripting facade); image files pass through unchanged.
    const resolveFile = async (file) => {
      if (!isVideoFile(file)) return file;
      try {
        return await videoFileToImageFile(file, Number(frameEl && frameEl.value) || 0);
      } catch (e) {
        notify(`Could not capture a video frame — ${e.message}`, 'err');
        return null;
      }
    };

    hereBtn.addEventListener('click', async () => {
      const file = fileEl.files && fileEl.files[0];
      if (!file) return;
      const address = (targetEl && targetEl.value) || null;
      const resolved = await resolveFile(file);
      if (!resolved) return;
      app.openImageHere(resolved, incog.checked, address);
      close();
    });
    newTabBtn.addEventListener('click', async () => {
      const file = fileEl.files && fileEl.files[0];
      if (!file) return;
      const resolved = await resolveFile(file);
      if (!resolved) return;
      app.openImageNewTab(resolved, incog.checked);
      close();
    });
  }
}
define('stencil-open-image-modal', StencilOpenImageModal);
