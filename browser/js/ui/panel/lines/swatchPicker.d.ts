// The Lines tab's colour picker: one hidden colour field for the list, anchored to the swatch that
// asked. A pick sets that row's line or point colour through applyLineChange, keeping its opacity.
import type { DrawingApp } from '../../../core/drawingApp.js';

/** Which of a line's colours a swatch edits. */
export type SwatchColor = 'color' | 'pointColor';

export interface SwatchPicker {
  /** Opens the native picker under `swatch` for line `idx`'s `prop` colour; inert in a read-only compare view. */
  open(app: DrawingApp, idx: number, prop: SwatchColor, swatch: Element | null): void;
  /** One undo step: the line colour back to the toolbar's (`app.color`), the point colour to '' (its line's). */
  reset(app: DrawingApp, idx: number, prop: SwatchColor): void;
}

/** Appends the hidden field to `host`, which must outlive the list's re-renders. */
export declare const createSwatchPicker: (host: Element) => SwatchPicker;
