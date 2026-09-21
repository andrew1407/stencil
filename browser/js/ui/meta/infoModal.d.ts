import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

/** The searchable Controls & Shortcuts info modal (config/infoConfig.json). */
export declare class StencilInfoModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
