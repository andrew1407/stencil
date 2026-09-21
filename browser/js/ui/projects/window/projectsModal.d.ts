import type { DrawingApp } from '../../../core/drawingApp.js';
import type { StencilElement } from '../../base.js';

/** The projects chooser/switcher; rows are built at runtime by wire(). */
export declare class StencilProjectsModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
