// The Lines tab's row gestures, delegated once per list body: hover, select, the line and point
// colour swatches, the inline numbers and name, the eye, the bin and Delete.
import type { DrawingApp } from '../../../core/drawingApp.js';

/** Wires `body` once and points every listener at `app`, the latest render's; `host` keeps the picker field. */
export declare const wireLinesList: (body: HTMLTableSectionElement, host: Element, app: DrawingApp) => void;

/** Whether row `i` is the one whose eye was just clicked; true once, then that toggle has played. */
export declare const takeEyeToggled: (i: number) => boolean;
