export declare const SURFACE_IN_MS: number;
export declare const SURFACE_OUT_MS: number;
export declare const SURFACE_MENU_IN_MS: number;
export declare const SURFACE_MENU_OUT_MS: number;
export declare const SURFACE_MOTE_PX: number;
export declare const SURFACE_COLS: number;
export declare const SURFACE_ROWS: number;
export declare const SURFACE_SPECK_PX: number;
export declare const SURFACE_SPREAD: number;
export declare const SURFACE_FORMING_CLASS: string;
export declare const SURFACE_LEAVING_CLASS: string;
export declare const SURFACE_DRIVEN_CLASS: string;

export interface MoteFlight { delay: number; dx: number; dy: number; mx: number; my: number; rot: number; scale: number; }
export interface Box { width: number; height: number; left?: number; top?: number; }
export interface Point { x: number; y: number; }

/** One mote's flight when a whole surface gathers into — or bursts out of — a single point. */
export declare function surfaceMotion(
  cx: number, cy: number, cols: number, rows: number, box: Box | null, point: Point | null,
  opts?: { span?: number; spread?: number },
): MoteFlight;

export declare function centerOf(elOrRect: Element | Box | null | undefined): Point | null;
