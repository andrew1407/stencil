export interface Rect { left: number; top: number; bottom: number; }
export interface Box { width: number; height: number; }
export interface Viewport { width: number; height: number; }

/** Below the anchor, flipped above on overflow, clamped inside the viewport on both axes. */
export declare function popoverPosition(args: {
  anchor: Rect; box: Box; viewport: Viewport; gap?: number; margin?: number;
}): { left: number; top: number };
