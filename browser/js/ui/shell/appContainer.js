import { StencilElement, hostTag, define } from '../base.js';
import { StencilToolbar } from '../toolbar/toolbar.js';
import { StencilSelectionPanel } from '../panel/selectionPanel.js';
import { StencilImageInfo } from '../panel/imageInfo.js';
import { StencilMainContent } from '../panel/mainContent.js';
export class StencilAppContainer extends StencilElement {
  static inner() {
    return `${StencilToolbar.template()}${StencilSelectionPanel.template()}${StencilImageInfo.template()}${StencilMainContent.template()}`;
  }
  static template() { return hostTag('stencil-app-container', 'class="container"', StencilAppContainer.inner()); }
}
define('stencil-app-container', StencilAppContainer);
