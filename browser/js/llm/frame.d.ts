// §1 coordinate re-mapping (executor-side). Plan coordinates are in the frame of the
// working image the model SAW; crop/rotate change it mid-plan, so the executor composes a
// running affine map (quarter turns + integer translations, exact arithmetic).

/** An affine map: x' = a·x + b·y + tx, y' = c·x + d·y + ty. */
export interface Frame { a: number; b: number; c: number; d: number; tx: number; ty: number; }

export interface FramePoint { x: number; y: number; }

export declare const identityFrame: () => Frame;
/** Compose `n` AFTER `f` (points flow f → n), mutating `f` in place. */
export declare const composeFrame: (f: Frame, n: Frame) => void;
export declare const mapFramePoint: (f: Frame, p: FramePoint) => FramePoint;
