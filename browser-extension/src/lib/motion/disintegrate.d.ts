export interface TileCell { cx: number; cy: number; cols: number; rows: number; cellW: number; cellH: number; }
export interface Speck { color: string; alpha: number; px: number; glint: boolean; }

export interface DisintegrateOptions {
  cols?: number;
  rows?: number;
  gather?: boolean;
  toward?: { x: number; y: number } | null;
  ms?: number;
  px?: number;
  spread?: number;
  toBody?: boolean;
  hostEl?: Element | null;
  hostClass?: string;
  paintTile?: ((cell: TileCell) => Speck) | null;
}

/** Scatters `el` into motes; false when reduced motion/no particles/the element is too small. */
export declare function disintegrate(el: Element, opts?: DisintegrateOptions): boolean;
/** The same flight, played backwards — every mote starts scattered and flies home. */
export declare function reintegrate(el: Element, opts?: DisintegrateOptions): boolean;
