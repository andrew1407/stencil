export interface GenieRect { left: number; top: number; width: number; height: number; }
export type GenieFlow = 'up' | 'down' | 'left' | 'right';
export interface GenieBand {
  /** Where the band lands, in client px. */
  target: GenieRect;
  /** The band of the picture, as fractions (0–1). */
  source: { x: number; y: number; w: number; h: number };
}

export declare const GENIE_LAG: number;
export declare const GENIE_PINCH_END: number;
export declare const GENIE_BEND: number;
export declare const GENIE_MAX_ICON_PX: number;

/** The rect a window flows into: `icon`, or a 24px point at its centre once its area passes a GENIE_MAX_ICON_PX square. */
export declare function genieSink(icon: GenieRect): GenieRect;

/** The edge of `box` the point lies beyond, the widest gap winning; null over the box. */
export declare function genieSide(box: GenieRect, x: number, y: number): GenieFlow | null;
/** The 2px strip of `r` facing `box` (above or below it), or `r` whole where the two overlap. */
export declare function genieFacing(r: GenieRect, box: GenieRect): GenieRect;

/** The direction the bands flow toward the icon: the edge it lies beyond, else its direction from the centre. */
export declare function genieFlowSide(win: GenieRect, icon: GenieRect): GenieFlow;
/** How far picture row `v` (0 = far edge, 1 = near edge) has travelled at progress `p` (0–1). */
export declare function genieTravel(v: number, p: number): number;
/** The bands at progress `p`: 0 = the window at rest, 1 = shrunk onto the icon. */
export declare function genieWarp(win: GenieRect, icon: GenieRect, side: GenieFlow, p: number): GenieBand[];
/** The warp's silhouette at `p`: 2 × (rows + 1) client points, a fixed count (the theme swap's Sliding mask). */
export declare function genieOutline(win: GenieRect, icon: GenieRect, side: GenieFlow, p: number, rows?: number): Array<{ x: number; y: number }>;
