import type { DrawingApp } from '../../core/drawingApp.js';

/** One swatch a colour drag can lift a colour from or drop one on. */
export interface ColorSwatch {
  /** The element that glows and is dragged: the field, its well, the row swatch or the registered control. */
  el: Element;
  /** Carries an alpha byte (a field with its opacity box, a line's own colour). */
  alpha: boolean;
  /** The colour it shows: `#rrggbb`, or `#rrggbbaa` when translucent; null when it has none. */
  read(): string | null;
  /** Takes `color` through the swatch's own change path, as a pick would. */
  apply(color: string): void;
  enabled(): boolean;
}

/** The Lines-tab row swatch classes and the line property each shows. */
export declare const ROW_SWATCHES: Readonly<Record<string, 'color' | 'pointColor'>>;
export declare const parseSwatchColor: (value: unknown) => { hex: string; alpha: number } | null;
export declare const liftableColor: (value: unknown) => { hex: string; alpha: number } | null;
export declare const colorToApply: (source: ColorSwatch, target: ColorSwatch) => string | null;
export declare const bindSwatchApp: (app: DrawingApp | null) => void;
export declare const registerColorSwatch: (el: Element | null, adapter: {
  read(): string | null;
  apply(color: string): void;
  alpha?: boolean;
  enabled?(): boolean;
}) => void;
export declare const swatchOf: (node: Element | null) => ColorSwatch | null;
export declare const liveSwatches: (doc?: Document) => ColorSwatch[];
