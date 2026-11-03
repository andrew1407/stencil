import type { Point } from './surfaceMotion.js';

/** Drop whatever `el` has in flight (cloud and driving classes), leaving the end state untouched. */
export declare function settleSurface(el: Element | null | undefined): void;
export declare function surfaceIn(el: Element | null | undefined, point: Point, opts?: { ms?: number }): boolean;
export declare function surfaceOut(el: Element | null | undefined, point: Point, opts?: { ms?: number }): boolean;

/** The Sliding flight's lengths (ms). */
export declare const GENIE_IN_MS: number;
export declare const GENIE_OUT_MS: number;
/** Ends a genie flight at once, running its pending unmount. */
export declare function settleGenie(box: HTMLElement | null | undefined): void;
/** In Sliding, flies `box` out of `from` (an element or client rect; null pops in place); false otherwise. */
export declare function genieIn(box: HTMLElement | null | undefined, from: Element | DOMRect | null | undefined,
                                opts?: { ms?: number }): boolean;
/** In Sliding, flies `box` home and runs `done` as it lands; false (and `done` not run) otherwise. */
export declare function genieOut(box: HTMLElement | null | undefined, from: Element | DOMRect | null | undefined,
                                 done?: () => void, backdrop?: HTMLElement | null): boolean;
/** The instant exit for a box hidden at once: genieIn's picture, or a fresh one, poured into `to`. False out of Sliding. */
export declare function genieAway(box: HTMLElement | null | undefined, to: Element | DOMRect | null | undefined,
                                  opts?: { ms?: number }): boolean;
/** Transition's entrance: the box's own CSS flight out of `from` (no picture). False out of Transition. */
export declare const growIn: (box: HTMLElement | null | undefined, from: Element | DOMRect | null | undefined) => boolean;
/** Transition's exit back into `from`; `done` (the unmount) runs as it lands. False, `done` not run, out of Transition. */
export declare const growOut: (box: HTMLElement | null | undefined, from: Element | DOMRect | null | undefined,
                               done?: () => void, backdrop?: HTMLElement | null) => boolean;
/** A menu's pour length (ms): quicker than a window's. */
export declare const MENU_GENIE_MS: number;
/** A short strip at a point, for a menu that opens out of the click. */
export declare const pointBox: (p: { x: number; y: number } | null | undefined) => { left: number; top: number; width: number; height: number } | null;
/** A menu hidden at once leaving: Sliding's pour into `to`, Transition's shrinking copy; false for the dust. */
export declare const menuAway: (el: HTMLElement | null | undefined, to: Element | DOMRect | object | null | undefined) => boolean;
/** A menu arriving: Sliding's pour out of `from`; false out of Sliding. */
export declare const menuIn: (el: HTMLElement | null | undefined, from: Element | DOMRect | object | null | undefined) => boolean;
