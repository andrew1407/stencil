// Start / stop drawing, and which shape. Starting either continues the selected line
// (adopting its style) or opens a fresh stroke; stopping commits whatever was drawn.
import type { DrawingApp } from '../drawingApp.js';

export type DrawMode = 'line' | 'rect';
export interface StartDrawingOptions {
  /** false: never continue the selected line, always open a fresh stroke. */
  connect?: boolean;
  /** Keep the selection (and its panel) when opening a fresh stroke. */
  keepSelection?: boolean;
}

export declare const startDrawingMode: (app: DrawingApp, opts?: StartDrawingOptions) => void;
export declare const setDrawMode: (app: DrawingApp, mode: DrawMode | string) => void;
export declare const stopDrawingMode: (app: DrawingApp) => void;
