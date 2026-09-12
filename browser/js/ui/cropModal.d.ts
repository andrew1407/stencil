import type { StencilElement } from './base.js';

/** The crop modal: a rect over the original image, locked to the page aspect (core/cropGeometry.js). */
export declare class StencilCropModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: object): void;
}
