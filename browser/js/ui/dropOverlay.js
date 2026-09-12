import { StencilElement, hostTag, define } from './base.js';
import { icon } from './icons.js';
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
        <div class="drop-foot">…or drop a .json layout / .stencil project file (either side — the split above is for images)</div>
    `;
  }
  static template() { return hostTag('stencil-drop-overlay', 'id="global-drop-overlay"', StencilDropOverlay.inner()); }
}
define('stencil-drop-overlay', StencilDropOverlay);

// Shown instantly, hidden on .drop-closing (pointer-events:none, so it cannot eat the drop).
// Both helpers are idempotent: dragover fires them every frame.
const DROP_CLOSE_MS = 220;   // matches .drop-closing in animations/dust.css

export const showDropOverlay = (el) => {
  if (!el) return;
  clearTimeout(el._dropCloseTimer);
  el.classList.remove('drop-closing');
  el.style.display = 'flex';
};

export const hideDropOverlay = (el) => {
  if (!el || el.style.display === 'none' || el.style.display === '') return;
  if (el.classList.contains('drop-closing')) return;
  el.classList.add('drop-closing');
  el._dropCloseTimer = setTimeout(() => {
    el.classList.remove('drop-closing');
    el.style.display = 'none';
  }, DROP_CLOSE_MS);
};
