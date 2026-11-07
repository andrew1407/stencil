// A box moved between two client rects by a FLIP, or glided by its real geometry.
export interface ClientBox { left: number; top: number; width: number; height: number }
/** The CSS transform-origin (px) where a row's arrow meets its flyout, inside `rect`. */
export declare const menuPopOrigin: (x: number, y: number, rect: ClientBox | DOMRect) => string;
export declare const FLIP_MS: number;
export declare const FLIP_EASING: string;
export declare const FLIP_ACTIVE_CLASS: string;
/** The transform that maps `to` back onto `from`; null without two boxes. */
export declare function flipTransform(from: ClientBox | DOMRect | null | undefined, to: ClientBox | DOMRect | null | undefined): string | null;
/** FLIP `el` from `from` to where it now stands. */
export declare function flipFrom(el: HTMLElement | null | undefined, from: ClientBox | DOMRect | null | undefined,
  opts?: { ms?: number; activeClass?: string }): void;
/** Glide `el` from `from` by its real geometry, held fixed for the flight; call after the layout change. */
export declare function glideFrom(el: HTMLElement | null | undefined, from: ClientBox | DOMRect | null | undefined,
  opts?: { ms?: number; activeClass?: string }): void;
