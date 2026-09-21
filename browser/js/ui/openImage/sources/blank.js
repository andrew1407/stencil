// The Open-Image window's Blank tab: the fill colour with its two presets, and the pixel size.
// A blank canvas has no source and no preview, so Create is the window's footer button asking
// this tab what it holds.
import { StencilElement, hostTag, define } from '../../base.js';
import { notify } from '../../../utils.js';
import { defaultBlankSizePx } from '../../../core/layout.js';

const MIN_PX = 1, MAX_PX = 8192;

export class StencilOiBlankTab extends StencilElement {
  static inner() {
    return `
                <div class="oi-panel" id="oi-panel-blank" data-panel="blank" style="display:none">
                    <div class="vs-section">Fill color</div>
                    <div class="vs-row"><label>Presets</label>
                        <span class="bi-presets">
                            <button id="blank-image-white" class="bi-preset bi-preset-white" type="button" data-title="Fill with white">White</button>
                            <button id="blank-image-black" class="bi-preset bi-preset-black" type="button" data-title="Fill with black">Black</button>
                        </span>
                    </div>
                    <!-- The well reads its hex beside the chip, as the desktop swatch does
                         (setColorSwatch withHex) — one control, not a bare picker. -->
                    <div class="vs-row"><label>Custom color</label><label class="oi-color"><input type="color" id="blank-image-color" value="#ffffff"><span class="oi-hex" id="blank-image-color-hex">#FFFFFF</span></label></div>
                    <div class="vs-section">Size (px)</div>
                    <div class="vs-row"><label>Width</label><input type="number" id="blank-image-width" min="1" max="8192"></div>
                    <div class="vs-row"><label>Height</label><input type="number" id="blank-image-height" min="1" max="8192"></div>
                </div>`;
  }

  static template() {
    return hostTag('stencil-oi-blank-tab', 'style="display:contents"', StencilOiBlankTab.inner());
  }

  // Every route that writes the fill writes the hex with it: the presets set the SAME
  // value the picker holds (desktop parity — both land on customColor).
  #syncHex() {
    const hex = this.$('blank-image-color-hex');
    if (hex) hex.textContent = String(this.$('blank-image-color').value || '').toUpperCase();
  }

  /** The size a fresh open offers: the project's own page, in pixels. */
  reset(pageDims) {
    this.$('blank-image-color').value = '#ffffff';
    this.#syncHex();
    const px = defaultBlankSizePx(pageDims);
    this.$('blank-image-width').value = px.width;
    this.$('blank-image-height').value = px.height;
  }

  /** Asked by the window's Create: validates, then announces what to make. */
  requestCreate() {
    const width = parseInt(this.$('blank-image-width').value, 10);
    const height = parseInt(this.$('blank-image-height').value, 10);
    const ok = (n) => n >= MIN_PX && n <= MAX_PX;
    if (!ok(width) || !ok(height)) {
      notify(`Width and height must be ${MIN_PX}–${MAX_PX} px`, 'fail');
      return;
    }
    this.emit('create-blank', { color: this.$('blank-image-color').value, width, height });
  }

  wire() {
    const color = this.$('blank-image-color');
    color.addEventListener('input', () => this.#syncHex());
    this.$('blank-image-white').addEventListener('click', () => { color.value = '#ffffff'; this.#syncHex(); });
    this.$('blank-image-black').addEventListener('click', () => { color.value = '#000000'; this.#syncHex(); });
  }
}

define('stencil-oi-blank-tab', StencilOiBlankTab);
