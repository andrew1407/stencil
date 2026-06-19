import { StencilElement, hostTag, define } from './base.js';
import { icon } from './icons.js';
// ── Component: global drag-and-drop overlay ─────────────────────
export class StencilDropOverlay extends StencilElement {
  static inner() {
    return `
        <div class="drop-message">
            <div class="drop-icon">${icon('upload', { size: 52 })}</div>
            <p>Drop anywhere to load</p>
            <span>Image file → sets background &nbsp;|&nbsp; .json file → applies drawing data</span>
        </div>
    `;
  }
  static template() { return hostTag('stencil-drop-overlay', 'id="global-drop-overlay"', StencilDropOverlay.inner()); }
}
define('stencil-drop-overlay', StencilDropOverlay);
