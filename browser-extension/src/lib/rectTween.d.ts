export interface TweenRect { x: number; y: number; width: number; height: number; }

/** The crop rect's flight length in ms, on the dialog's height-ease clock. */
export const RECT_TWEEN_MS: number;

/** QEasingCurve::OutCubic. */
export function easeOutCubic(t: number): number;

/**
 * Eases a rect from `from` to `to`, calling `onFrame` per frame (`from` at once, `to` last).
 * Returns a cancel; cancelled, no further frame is delivered. Without `from`, without
 * requestAnimationFrame, or under reduced motion, `to` is delivered immediately.
 */
export function tweenRect(
  from: TweenRect | null | undefined,
  to: TweenRect,
  onFrame: (rect: TweenRect) => void,
  ms?: number,
): () => void;
