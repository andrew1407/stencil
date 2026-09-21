import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

/** Fullscreen trigger zones + slide-in panels; exposes app.toggleFullscreen. */
export declare class StencilFullscreenLayer extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
