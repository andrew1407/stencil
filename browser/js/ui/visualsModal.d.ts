import type { DrawingApp } from '../core/drawingApp.js';
import type { StencilElement } from './base.js';

/** The visual defaults modal: accent, appearance, motion, and default line/fill styling. */
export declare class StencilVisualsModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
