// The golden shining a show's notice wears, following the pill's own rounded rectangle.
/** One breath of the halo around a w×h pill at (x, y). */
export declare const paintToastGlow: (ctx: CanvasRenderingContext2D, x: number, y: number,
  w: number, h: number, beat: number) => void;
/** Runs for the toast's life; returns the stop the caller calls when it leaves. */
export declare const attachToastGlow: (toast: HTMLElement, doc?: Document) => () => void;
