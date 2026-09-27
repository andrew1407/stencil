// The editor's two stacked canvases: #canvas holds the picture and repaints only when it
// changes; the overlay above it holds the lines and repaints every frame. Unlayered (no
// overlay attached), everything paints on #canvas every frame.

export declare class StageLayers {
  /** Take the overlay canvas; a canvas without a 2D context leaves the stage unlayered. */
  attach(canvas: HTMLCanvasElement | null): void;
  readonly overlay: HTMLCanvasElement | null;
  readonly ctx: CanvasRenderingContext2D | null;
  /** True, and then remembered, when #canvas does not hold this key's picture; always true unlayered. */
  stale(key: readonly unknown[]): boolean;
  /** Forget what #canvas holds, so the next frame repaints it. */
  invalidate(): void;
  /** Empty the overlay at `canvas`'s backing size; false when unlayered. */
  clear(canvas: { width: number; height: number }): boolean;
  /** Bottom first: `canvas`, then the overlay when there is one. */
  layers<C>(canvas: C): (C | HTMLCanvasElement)[];
}
