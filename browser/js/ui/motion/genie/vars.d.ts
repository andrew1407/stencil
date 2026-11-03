export interface GenieBox { left: number; top: number; width: number; height: number; }

export declare const GENIE_TILT_DEG: number;
export declare const GENIE_STRETCH: number;
export declare const GENIE_PINCH: number;
export declare const GENIE_SQUASH: number;
export declare const GENIE_BULGE: number;
export declare const GENIE_DEPTH: number;
export declare const GENIE_POP: Readonly<{ kx: number; ky: number; ox: number; oy: number }>;
/** Every custom property genieVars returns. */
export declare const GENIE_VAR_NAMES: readonly string[];

/** The edge of `box` the point lies beyond, the widest gap winning; null when it is over the box. */
export declare function genieSide(box: GenieBox, x: number, y: number): 'up' | 'down' | 'left' | 'right' | null;

/** The `--modal-*` properties the window flight keyframes read; null for an unmeasured box. `from` null = from above. */
export declare function genieVars(box: GenieBox | null | undefined, from: GenieBox | null | undefined): Record<string, string> | null;

/** Stamps genieVars on `el`, clearing a stale set first; false when the box is unmeasured. */
export declare function applyGenieVars(el: HTMLElement | null | undefined, box: GenieBox | null | undefined,
                               from: GenieBox | null | undefined): boolean;

/** Removes every property genieVars sets. */
export declare function clearGenieVars(el: HTMLElement | null | undefined): void;
