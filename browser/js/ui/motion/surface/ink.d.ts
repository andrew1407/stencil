/** The 2D-context members the ink sheet draws through. */
export interface InkContext {
  font: string; textBaseline: string; fillStyle: string; strokeStyle: string;
  lineWidth: number; lineCap: string; lineJoin: string;
  save(): void; restore(): void;
  translate(x: number, y: number): void; scale(x: number, y: number): void;
  fillText(text: string, x: number, y: number): void;
  stroke(path: Path2D): void;
}

/** The share of a cell that must be inked for it to fly. */
export declare const INK_FLOOR: number;

/** Stroke one inline glyph at its place on the sheet, scaled out of its viewBox. */
export declare const svgInk: (ctx: InkContext, svg: Element, ox: number, oy: number) => void;

/** Draw every text node under `el`, and every inline glyph, at their measured boxes. */
export declare const textInk: (ctx: InkContext, el: Element, ox: number, oy: number,
                               get: (node: Element) => CSSStyleDeclaration) => void;

/** One alpha per cell, row-major; null when the ink could not be measured. */
export declare const inkAlpha: (el: Element, cols: number, rows: number) => Float32Array | null;
