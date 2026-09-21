export interface Rect { left: number; top: number; bottom: number; }
export interface Box { width: number; height: number; }
export interface Viewport { width: number; height: number; }

/** Below the anchor, flipped above when above has more room, clamped inside the viewport. */
export declare function popoverPosition(args: {
  anchor: Rect; box: Box; viewport: Viewport; gap?: number; margin?: number;
}): { left: number; top: number };
