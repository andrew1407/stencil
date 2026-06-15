import { StencilElement, hostTag, define } from './base.js';
// ── Component: image info line ──────────────────────────────────
export class StencilImageInfo extends StencilElement {
  static inner() { return `No image loaded. Upload an image to start.`; }
  static template() { return hostTag('stencil-image-info', 'class="info" id="image-info"', StencilImageInfo.inner()); }
}
define('stencil-image-info', StencilImageInfo);
