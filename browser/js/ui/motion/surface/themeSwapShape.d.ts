/** Transition's wipe: the new palette's clip from the left edge to the whole viewport. */
export declare const WIPE_CLIP_FROM: string;
export declare const WIPE_CLIP_TO: string;
/** Sliding's pour: its length (ms), keyframes name, frame and silhouette-row counts. */
export declare const SWAP_GENIE_MS: number;
export declare const SWAP_GENIE_KEYFRAMES: string;
export declare const SWAP_GENIE_STEPS: number;
export declare const SWAP_GENIE_ROWS: number;
/** The box edge nearest the control at (x, y): the way a theme change pours. */
export declare function swapFlowSide(x: number, y: number, w: number, h: number): 'up' | 'down' | 'left' | 'right';
/** The genie silhouette at progress `p` (1 = in its source, 0 = the box) as a clip-path polygon: out of the
 * control, or `fromTop` (an accent change) down from the whole top edge to the bottom. */
export declare function swapGeniePolygon(x: number, y: number, w: number, h: number, p: number, fromTop?: boolean): string;
/** The warp progress at time `t` (0..1): out-sine for a theme change, in-out sine for an accent's sweep down. */
export declare const swapProgress: (t: number, fromTop?: boolean) => number;
/** The @keyframes text Sliding's swap plays; '' with no viewport to measure. */
export declare function swapGenieKeyframes(x: number, y: number, w: number, h: number, fromTop?: boolean): string;
