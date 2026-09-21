/** Viewport-clamped offset for a dragged window; keeps its header reachable. */
export declare function clampOffset(
  box: { left: number; top: number; width: number; height: number },
  viewport: { width: number; height: number },
  dx: number,
  dy: number,
): { dx: number; dy: number };

/** Makes a modal's header drag its box. `reset()` drops the offset (called on every open). */
export declare function wireModalDrag(
  overlay: HTMLElement | null,
  boxOf: () => HTMLElement | null,
): { reset: () => void };
