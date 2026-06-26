import { StencilElement, hostTag, define, wireModalShell, fillTargetSelect } from './base.js';
import { notify } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
import { defaultBlankSizePx } from '../core/layout.js';
import { icon } from './icons.js';
const { PAGE_SIZES } = constants;

// ── Component: blank-image creator modal ────────────────────────
// Generates a solid-color image at a chosen pixel size (default: current page at
// 96 dpi), fed through the normal upload path (loadImageFromFile) so it behaves
// like an uploaded file. Opened from the idle-canvas icon and projects modal footer.
export class StencilBlankImageModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('image', { size: 18 })} New Blank Image</h2>
                <button class="app-modal-close btn-icon-text" id="blank-image-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="vs-section">Fill color</div>
                <div class="vs-row"><label>Presets</label>
                    <span class="bi-presets">
                        <button id="blank-image-white" class="bi-preset bi-preset-white" title="Fill with white">White</button>
                        <button id="blank-image-black" class="bi-preset bi-preset-black" title="Fill with black">Black</button>
                    </span>
                </div>
                <div class="vs-row"><label>Custom color</label><input type="color" id="blank-image-color" value="#ffffff"></div>
                <div class="vs-section">Size (px)</div>
                <div class="vs-row"><label>Width</label><input type="number" id="blank-image-width" min="1" max="8192"></div>
                <div class="vs-row"><label>Height</label><input type="number" id="blank-image-height" min="1" max="8192"></div>
                <!-- Save target: only shown when at least one server is connected. -->
                <div class="vs-row" id="blank-image-target-row" style="display:none">
                    <label title="Create this image locally or on a connected server">Save to</label>
                    <select id="blank-image-target"></select>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Size defaults match the current page size.</span>
                <button id="blank-image-create" class="btn-icon-text">${icon('image')}<span>Create</span></button>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-blank-image-modal', 'id="blank-image-modal-overlay" class="app-modal-overlay"', StencilBlankImageModal.inner()); }

  wire(app) {
    const overlay = document.getElementById('blank-image-modal-overlay');
    const closeBtn = document.getElementById('blank-image-close');
    const widthEl = document.getElementById('blank-image-width');
    const heightEl = document.getElementById('blank-image-height');
    const colorEl = document.getElementById('blank-image-color');
    const targetEl = document.getElementById('blank-image-target');
    const targetRow = document.getElementById('blank-image-target-row');

    // Page dimensions for size defaults. NOT getPageDimensions(): that swaps to
    // landscape from the CURRENT canvas aspect, meaningless when sizing a new image
    // — use the page as selected (portrait for named sizes, entered W×H for custom).
    const pageDims = () => (app.pageSize === 'custom'
      ? { width: app.customPageWidth, height: app.customPageHeight }
      : PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);

    const { open, close } = wireModalShell(overlay, document.getElementById('create-blank-btn'), closeBtn, {
      onOpen: () => {
        const px = defaultBlankSizePx(pageDims());
        widthEl.value = px.width;
        heightEl.value = px.height;
        fillTargetSelect(targetEl, targetRow, app.connections);
      }
    });

    // Second entry point: the projects modal footer. Close that modal (via its
    // own close button so its handlers run) before opening this one.
    document.getElementById('projects-blank-image').addEventListener('click', () => {
      document.getElementById('projects-close').click();
      open();
    });

    document.getElementById('blank-image-white').addEventListener('click', () => { colorEl.value = '#ffffff'; });
    document.getElementById('blank-image-black').addEventListener('click', () => { colorEl.value = '#000000'; });

    document.getElementById('blank-image-create').addEventListener('click', async () => {
      const w = parseInt(widthEl.value), h = parseInt(heightEl.value);
      if (!(w >= 1 && w <= 8192) || !(h >= 1 && h <= 8192)) {
        notify('Width and height must be 1–8192 px', 'fail');
        return;
      }
      if (app.image && !(await app.confirm('Replace the current image with a new blank image?', { title: 'Replace image' }))) return;
      const address = (targetEl && targetEl.value) || undefined;
      app.createBlankImage({ color: colorEl.value, width: w, height: h, address })
        .then(() => { close(); notify(`Blank ${w}×${h} image created`, 'ok'); })
        .catch((err) => notify(err && err.message ? err.message : 'Could not create the image', 'fail'));
    });
  }
}
define('stencil-blank-image-modal', StencilBlankImageModal);
