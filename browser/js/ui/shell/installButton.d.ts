import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

/** Install web app (PWA) / Download desktop app affordance (config/installConfig.json). */
export declare class StencilInstall extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
