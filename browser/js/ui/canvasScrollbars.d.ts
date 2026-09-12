export declare const SB_MIN_THUMB_PX: number;

/** A thumb's length and offset along a `track`-px run; null when nothing overflows. */
export declare const thumbMetrics: (
  client: number, scroll: number, offset: number, track: number,
) => { len: number; pos: number } | null;

export interface CanvasScrollbars {
  layout(): { canY: boolean; canX: boolean };
  reveal(): void;
  bars: { y: unknown; x: unknown };
}

/** Wires overlay scrollbars onto the canvas viewport; idempotent (cached on the element). */
export declare const wireCanvasScrollbars: (vp: HTMLElement | null | undefined) => CanvasScrollbars | null;
