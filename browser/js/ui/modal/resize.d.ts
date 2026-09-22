/** A window's edges resize it, the way the desktop's do; the held size clears on every open. */
export declare const MIN_W: number;
export declare const MIN_H: number;
/** The cursor for each edge name. */
export declare const CURSORS: Readonly<Record<string, string>>;

export type Rect = { left: number; top: number; width: number; height: number };

/** The edge a point touches ('n', 'se', …), or '' inside the box or away from it. */
export declare function edgeAt(box: Rect | null, x: number, y: number, band?: number): string;

/** The rect after dragging edge `dir` by (dx, dy); the opposite edge stays put. */
export declare function resizeRect(
  r: Rect,
  dir: string,
  dx: number,
  dy: number,
  viewport: { width: number; height: number },
  limits?: { minW?: number; minH?: number; margin?: number },
): Rect;

/** Makes a modal's edges resize its box. `reset()` drops the held size (called on every open). */
export declare function wireModalResize(
  overlay: HTMLElement | null,
  boxOf: () => HTMLElement | null,
): { reset: () => void };
