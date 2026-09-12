import type { DrawingApp } from '../core/drawingApp.js';
import type { StencilElement } from './base.js';

/** Markup only; inputs are wired by DrawingApp via global ids. */
export declare class StencilSelectionPanel extends StencilElement {
  static inner(): string;
  static template(): string;
}

/** The dust flight's origin/destination point, anchored to #image-info. */
export declare const barDustPoint: (el: HTMLElement, closing?: boolean) => { x: number; y: number };

/** Populate + show the selection panel (and its fullscreen mirror) for `line`. */
export declare function showSelectionPanel(app: DrawingApp, line: Record<string, unknown>): void;
/** Hide the selection panel and its fullscreen mirror. */
export declare function hideSelectionPanels(): void;
/** Apply the locked-area fill from the selection panel's controls to the selected line. */
export declare function applyFill(app: DrawingApp): void;
/** Rebuild + wire the fullscreen mirror of the panel for `line`. */
export declare function syncFsSelectionPanel(app: DrawingApp, line: Record<string, unknown> | null): void;
