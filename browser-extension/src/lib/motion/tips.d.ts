export declare const TIP_DUST_IN_MS: number;
export declare const TIP_DUST_OUT_MS: number;
export declare const TIP_SHOW_DELAY_MS: number;
export declare const TIP_GROW_CLASS: string;
export declare const TIP_SHRINK_CLASS: string;
/** Sliding's pour or Transition's grow out of `point`; its run in ms, 0 (or false) where neither plays. `landed` runs as it lands. */
export declare const tipFallbackIn: (el: HTMLElement | null | undefined, point: { x: number; y: number } | null,
  landed?: (() => void) | null) => number | false;
/** The matching exit; call it while the tip is still shown. Transition shrinks the tip itself, so it must stay laid out. */
export declare const tipFallbackOut: (el: HTMLElement | null | undefined, point: { x: number; y: number } | null) => boolean;

/** The centre of an element (or rect) — the point a popup's dust belongs to; null for a detached owner. */
export declare function rectCenter(
  elOrRect: Element | { width: number; height: number; left?: number; top?: number } | null | undefined,
): { x: number; y: number } | null;
