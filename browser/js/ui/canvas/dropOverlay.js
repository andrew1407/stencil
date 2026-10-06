import { StencilElement, hostTag, define } from '../base.js';
import { icon } from '../icons.js';
export class StencilDropOverlay extends StencilElement {
  static inner() {
    return `
        <div class="drop-split">
            <div class="drop-zone drop-zone-left" data-zone="left">
                <div class="drop-icon">${icon('upload', { size: 46 })}</div>
                <p>Upload &amp; save</p>
                <span>Load the image or .stencil project and keep it in your projects</span>
            </div>
            <div class="drop-zone drop-zone-right" data-zone="right">
                <div class="drop-icon">${icon('incognito', { size: 46 })}</div>
                <p>Upload incognito</p>
                <span>Load the image or .stencil project without saving it</span>
            </div>
            <div class="drop-zone drop-zone-single">
                <div class="drop-icon"></div>
                <p></p>
                <span></span>
            </div>
        </div>
        <div class="drop-foot">…or drop a .stc script on either side to run it</div>
    `;
  }
  static template() { return hostTag('stencil-drop-overlay', 'id="global-drop-overlay"', StencilDropOverlay.inner()); }
}
define('stencil-drop-overlay', StencilDropOverlay);

// The one full-width zone a drag that opens no image shows instead of the split.
const SINGLE = {
  layout: { icon: 'layers', title: 'Apply layout', sub: 'Draw the .json layout over the open image' },
  script: { icon: 'script', title: 'Run script', sub: 'Load the .stc script and run it' },
};
const NO_IMAGE_SUB = 'Open an image first — a layout needs one to draw on';

// `kind` from core/pointer/dropKind.js; idempotent, so dragover may call it every frame.
export const setDropKind = (el, kind, { hasImage = true } = {}) => {
  if (!el) return;
  const single = SINGLE[kind];
  el.classList.toggle('drop-single', !!single);
  if (!single || el.dataset.dropKind === `${kind}:${hasImage}`) return;
  el.dataset.dropKind = `${kind}:${hasImage}`;
  const zone = el.querySelector('.drop-zone-single');
  if (!zone) return;
  zone.querySelector('.drop-icon').innerHTML = icon(single.icon, { size: 46 });
  zone.querySelector('p').textContent = single.title;
  zone.querySelector('span').textContent = kind === 'layout' && !hasImage ? NO_IMAGE_SUB : single.sub;
};

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
