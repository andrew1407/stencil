// The Lines tab's colour picker: one hidden colour field for the list, anchored to the swatch that
// asked. A pick recolours the selected line through applySelectionChange, keeping its opacity.
import type { DrawingApp } from '../../core/drawingApp.js';

export interface SwatchPicker {
  /** Opens the native picker under `swatch` for line `idx`; only while that line is the selection. */
  open(app: DrawingApp, idx: number, swatch: Element | null): void;
  /** Recolours line `idx` to the toolbar's colour (`app.color`) as one undo step. */
  reset(app: DrawingApp, idx: number): void;
}

/** Appends the hidden field to `host`, which must outlive the list's re-renders. */
export declare const createSwatchPicker: (host: Element) => SwatchPicker;
