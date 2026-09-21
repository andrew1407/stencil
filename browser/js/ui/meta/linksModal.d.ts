import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

/** Edit the current image's provenance (source URL, resource page). */
export declare class StencilLinksModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
