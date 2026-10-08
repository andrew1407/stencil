/** Viewport-clamped offset for a dragged window; keeps its header reachable. */
export declare function clampOffset(
  box: { left: number; top: number; width: number; height: number },
  viewport: { width: number; height: number },
  dx: number,
  dy: number,
): { dx: number; dy: number };

/** The offset putting a window's top-left corner on a client point, the whole box inside the viewport. */
export declare function placeOffset(
  box: { left: number; top: number; width: number; height: number },
  point: { x: number; y: number },
  viewport: { width: number; height: number },
  margin?: number,
): { dx: number; dy: number };

export interface ModalDrag {
  /** Drops the offset (called on every open). */
  reset(): void;
  /** Puts the window's top-left corner on a client point; `opening` while its entrance is only starting. */
  placeAt(point: { x: number; y: number }, opening?: boolean): void;
}

/** Makes a modal's header drag its box. */
export declare function wireModalDrag(
  overlay: HTMLElement | null,
  boxOf: () => HTMLElement | null,
): ModalDrag;
