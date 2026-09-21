import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

export { syncWrappedSeparators, WRAPPED_SEP_CLASS } from './toolbarSeparators.js';
export { wireLogoColorPicker } from '../logo/logoAccent.js';

/** The toolbar (controls-wrapper + all control sections): markup + collapse/hints behaviour. */
export declare class StencilToolbar extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
