import type { ViewAnchor, ZoomPan } from '../../../core/zoom/pan.js';

/** The held zoom's step, added to the scale each repeat. */
export declare const HOLD_STEP: number;
/** ms between held steps. */
export declare const HOLD_REPEAT_MS: number;

/** One continuous step about the viewport centre; `sign` +1 zooms in, −1 out. */
export declare function holdStep(zp: ZoomPan, sign: number): void;

export interface HoldZoom {
  /** Ends the hold delay and the repeat. */
  stop(): void;
  /** The view the last press zoomed from, or null before any press. */
  pressAnchor(): ViewAnchor | null;
}

/** Click = a small step, double-press = a large one, hold = continuous zoom. */
export declare function setupHoldZoom(zp: ZoomPan, btn: HTMLElement, sign: number): HoldZoom;
