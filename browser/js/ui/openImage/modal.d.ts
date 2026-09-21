import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

/** The single way to get an image in: Local file / URL link / Blank tabs. */
export declare class StencilOpenImageModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
