import { StencilElement, hostTag, define } from './base.js';
import { icon } from './icons.js';
// ── Component: global drag-and-drop overlay ─────────────────────
export class StencilDropOverlay extends StencilElement {
  static inner() {
    return `
        <div class="drop-split">
            <div class="drop-zone drop-zone-left" data-zone="left">
                <div class="drop-icon">${icon('upload', { size: 46 })}</div>
                <p>Upload &amp; save</p>
                <span>Load the image and keep it in your projects</span>
            </div>
            <div class="drop-zone drop-zone-right" data-zone="right">
                <div class="drop-icon">${icon('incognito', { size: 46 })}</div>
                <p>Upload incognito</p>
                <span>Load the image without saving it</span>
            </div>
        </div>
        <div class="drop-foot">…or drop a .json file to apply drawing data</div>
    `;
  }
  static template() { return hostTag('stencil-drop-overlay', 'id="global-drop-overlay"', StencilDropOverlay.inner()); }
}
define('stencil-drop-overlay', StencilDropOverlay);
