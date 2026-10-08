// The Lines tab's row gestures, delegated once per list body: hover, select, the line and point
// colour swatches, the inline numbers, the bin and Delete.
import type { DrawingApp } from '../../../core/drawingApp.js';

/** Wires `body` once and points every listener at `app`, the latest render's; `host` keeps the picker field. */
export declare const wireLinesList: (body: HTMLTableSectionElement, host: Element, app: DrawingApp) => void;
