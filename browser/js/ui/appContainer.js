import { StencilElement, hostTag, define } from './base.js';
import { StencilToolbar } from './toolbar.js';
import { StencilSelectionPanel } from './selectionPanel.js';
import { StencilImageInfo } from './imageInfo.js';
import { StencilMainContent } from './mainContent.js';
// ── Component: .container wrapper composing toolbar + panels + main content ──
export class StencilAppContainer extends StencilElement {
  static inner() {
    return `${StencilToolbar.template()}${StencilSelectionPanel.template()}${StencilImageInfo.template()}${StencilMainContent.template()}`;
  }
  static template() { return hostTag('stencil-app-container', 'class="container"', StencilAppContainer.inner()); }
}
define('stencil-app-container', StencilAppContainer);
