import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

/** The hotkey editor: search, double-click-to-capture, per-row and global reset. */
export declare class StencilSettingsModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
