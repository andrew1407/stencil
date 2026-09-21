import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

export { syncWrappedSeparators, WRAPPED_SEP_CLASS } from './separators.js';
export { wireLogoColorPicker } from '../logo/accent.js';

/** The toolbar (controls-wrapper + all control sections): markup + collapse/hints behaviour. */
export declare class StencilToolbar extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
