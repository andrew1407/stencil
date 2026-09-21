import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

/** Canvas section + coordinates panel, and the panel's collapse. */
export declare class StencilMainContent extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
