import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

/** Provider + endpoint configuration modal (llm-contract.md §5). */
export declare class StencilLlmSettingsModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
}
