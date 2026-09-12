// Length-token parsing for the console API (pure). A position is a bare number (a pixel
// DELTA) or a unit string ('3cm', '-4in', '50%'); a leading '-' on a unit/percent token means
// "measured from the axis END", while on a bare number it keeps its arithmetic meaning.

export type LengthToken = number | string;
export type ParsedLength =
  | { kind: 'delta'; value: number }
  | { kind: 'px' | 'cm' | 'percent'; value: number; fromEnd: boolean };
/** null on bad input; 'cm' already converted from in/mm. */
export declare const parseLengthToken: (token: unknown) => ParsedLength | null;
/** An ABSOLUTE pixel coordinate on an axis, or null on unparseable input. */
export declare const resolveAxisPx: (token: unknown,
  axis: { lengthPx: number; pxPerCm: number; currentPx?: number }) => number | null;
/** Canonical page-size name ('A0'…'C10' or 'custom') for any casing; null otherwise. */
export declare const normalizePageSize: (s: unknown) => string | null;

/** The saved-layout subset the length measure reads. */
export interface MeasurableLayout {
  lines?: Array<{ points?: Array<{ x: number; y: number }> }>;
  imageWidth?: number; imageHeight?: number;
  pageSize?: string; customPageWidth?: number; customPageHeight?: number;
}
/** Total real-world length of every drawn segment, in cm; 0 when nothing is measurable. */
export declare const layoutLineLengthCm: (layout: MeasurableLayout | null | undefined) => number;
/** e.g. "A4 (21 × 29.7 cm)"; unknown names (incl. 'custom') echo back unchanged. */
export declare const pageFormatLabel: (name: string, unit?: string) => string;
/** <option> markup for every named format in PAGE_SIZES order. */
export declare const pageFormatOptions: (unit?: string) => string;
export declare const isDeltaToken: (token: unknown) => boolean;
