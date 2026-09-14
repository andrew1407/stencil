export declare const CHAT_DISINTEGRATE_COLS: number;
export declare const CHAT_DISINTEGRATE_ROWS: number;
export declare const SCATTER_TILE_BUDGET: number;
export declare const SCATTER_MAX_ROWS: number;
export declare function scatterGridFor(count: number, index?: number): { cols: number; rows: number };

export declare const DISINTEGRATE_MS: number;
export declare const DISINTEGRATE_COLS: number;
export declare const DISINTEGRATE_ROWS: number;
export declare const MIN_TILE_MS: number;
export declare const TILE_GATHER_SHARE: number;
export declare const TILE_JITTER_SHARE: number;

/** Deterministic per-tile jitter (a hash, not Math.random) — 0..1. */
export declare function tileNoise(cx: number, cy: number): number;

export declare const WAYPOINT_ALONG: number;
export declare const SWIRL_SHARE: number;
export declare const SWIRL_MAX_PX: number;
export declare function tileWaypoint(dx: number, dy: number, q: number): { mx: number; my: number };

export interface TileFlight { delay: number; dx: number; dy: number; mx: number; my: number; rot: number; scale: number; }
export declare function tileMotion(
  cx: number, cy: number, cols?: number, rows?: number, reverse?: boolean, span?: number,
): TileFlight;

export declare const MOTE_PX: number;
export declare function reshapeGrid(cols: number, rows: number, w: number, h: number, px?: number): { cols: number; rows: number };

export declare function retargetDust(el: Element | null | undefined): void;
export declare function cancelDust(el: Element | null | undefined): void;
export declare function flightOf(toward: unknown, gather: boolean): string;
