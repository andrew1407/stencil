export declare const TIP_DUST_IN_MS: number;
export declare const TIP_DUST_OUT_MS: number;
export declare const TIP_SHOW_DELAY_MS: number;

/** The centre of an element (or rect) — the point a popup's dust belongs to; null for a detached owner. */
export declare function rectCenter(
  elOrRect: Element | { width: number; height: number; left?: number; top?: number } | null | undefined,
): { x: number; y: number } | null;
