// Client px → canvas px, and the compare-divider grab band that shares the measurement.
import type { DrawingApp } from '../drawingApp.js';

/** CSS px within the canvas box plus image px; the box is measured once per frame. */
export interface CanvasPoint { cssX: number; cssY: number; x: number; y: number; }

export declare const canvasCoords: (app: DrawingApp, clientX: number, clientY: number) => CanvasPoint;
/** Within 8 CSS px of the split divider, and only in a split compare mode. */
export declare const nearCompareDivider: (app: DrawingApp, clientX: number, clientY: number) => boolean;
