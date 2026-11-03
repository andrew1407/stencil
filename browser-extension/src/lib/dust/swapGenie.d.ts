// Classic script (see accent.js): no ES exports — extends window.StencilKit with the palette swap's
// reveal per interface animation (Transition's wipe, Sliding's genie pour).

export interface SwapGenieKit {
  /** Sliding's pour length (ms). */
  SWAP_GENIE_MS: number;
  /** The genie silhouette at progress `p` (1 = in its source, 0 = the box): out of the control, or `fromTop` down from the top. */
  swapGeniePolygon(x: number, y: number, w: number, h: number, p: number, fromTop?: boolean): string;
  /** …and the @keyframes text that plays it; '' with no viewport to measure. */
  swapGenieKeyframes(x: number, y: number, w: number, h: number, fromTop?: boolean): string;
  /** Writes the reveal the interface animation asks for onto <html>. */
  writeSwapShape(root: HTMLElement, px: { x: number; y: number; w: number; h: number } | null, fromTop?: boolean): void;
}
