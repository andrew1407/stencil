import type { DrawingApp } from '../../core/drawingApp.js';

/** Live cursor-coordinate readout in the status bar below the canvas. */
export declare const updateCoordStatus: (app: DrawingApp, x?: number, y?: number) => void;

/** Reflect the active display unit across the UI (dropdown, custom size fields, table headers). */
export declare const applyUnitToUI: (app: DrawingApp) => void;
