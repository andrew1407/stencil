import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
import { defaultBlankSizePx } from '../core/layout.js';
const { PAGE_SIZES } = constants;

// ── Component: blank-image creator modal ────────────────────────
// Generates a solid-color image at a chosen pixel size (default: current page at
// 96 dpi) and feeds it through the normal upload path (loadImageFromFile) so it
// behaves like an uploaded file. Opened from the idle-canvas icon and the
// projects modal footer.
export class StencilBlankImageModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>🖼 New Blank Image</h2>
                <button class="app-modal-close" id="blank-image-close">✕ Close</button>
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
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Size defaults match the current page size.</span>
                <button id="blank-image-create">🖼 Create</button>
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

    // Page dimensions for the size defaults. Deliberately NOT getPageDimensions():
    // that swaps to landscape from the CURRENT canvas aspect, which is meaningless
    // while choosing the size of a new image — use the page as selected (portrait
    // for named sizes, the entered W×H for custom).
    const pageDims = () => (app.pageSize === 'custom'
      ? { width: app.customPageWidth, height: app.customPageHeight }
      : PAGE_SIZES[app.pageSize] || PAGE_SIZES.A4);

    const { open, close } = wireModalShell(overlay, document.getElementById('create-blank-btn'), closeBtn, {
      onOpen: () => {
        const px = defaultBlankSizePx(pageDims());
        widthEl.value = px.width;
        heightEl.value = px.height;
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

    document.getElementById('blank-image-create').addEventListener('click', () => {
      const w = parseInt(widthEl.value), h = parseInt(heightEl.value);
      if (!(w >= 1 && w <= 8192) || !(h >= 1 && h <= 8192)) {
        notify('Width and height must be 1–8192 px', 'fail');
        return;
      }
      if (app.image && !confirm('Replace the current image with a new blank image?')) return;
      const cnv = document.createElement('canvas');
      cnv.width = w;
      cnv.height = h;
      const ctx = cnv.getContext('2d');
      ctx.fillStyle = colorEl.value;
      ctx.fillRect(0, 0, w, h);
      cnv.toBlob(blob => {
        if (!blob) {
          notify('Could not create the image', 'fail');
          return;
        }
        app.loadImageFromFile(new File([blob], `blank-${w}x${h}.png`, { type: 'image/png' }));
        close();
        notify(`Blank ${w}×${h} image created`, 'ok');
      }, 'image/png');
    });
  }
}
define('stencil-blank-image-modal', StencilBlankImageModal);
