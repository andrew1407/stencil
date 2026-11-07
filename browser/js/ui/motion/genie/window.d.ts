import type { GenieRect } from './warp.js';

export declare const GENIE_HOLD_CLASS: string;
export declare const GENIE_DRIVEN_CLASS: string;
/** Sliding in force, motion allowed, and a DOM to photograph. */
export declare function genieEnabled(): boolean;

export interface WindowGenie {
  /** Flies the box out of `icon` onto `win` (client rects). */
  open(win: GenieRect, icon: GenieRect, zIndex?: number): void;
  /** Flies the box from `win` into `icon`; `landed` runs as it lands, or `fallbackMs` after its CSS flight starts. */
  close(win: GenieRect, icon: GenieRect, zIndex: number | undefined, landed: () => void, fallbackMs: number): void;
  /** Drops any flight or picture in progress and shows the box as it stands. */
  settle(): void;
}
export declare function createWindowGenie(boxOf: () => HTMLElement | null | undefined): WindowGenie;

/** Sliding's entrance for a box already placed (a dropdown list), poured out of `from`; false when Sliding is off.
 * `onFly` runs as the picture starts to move. */
export declare function genieIn(box: HTMLElement | null | undefined, from: Element | DOMRect | null | undefined,
                                opts?: { ms?: number; onFly?: () => void; clip?: DOMRect | null; landed?: () => void; keep?: boolean;
                                         /** An element the flight rides with (genie/flight.js). */ follow?: Element | null }): boolean;
/** Drops a box's genie, a picture still being taken included, and shows it as it stands. */
export declare function settleGenie(box: HTMLElement | null | undefined): void;
/** Sliding's exit for a box in place, poured into `to` (read after `beforeFly`); `done` runs as it lands. False when Sliding is off. */
export declare function genieOut(box: HTMLElement | null | undefined,
                                 to: Element | DOMRect | (() => Element | DOMRect | null) | null | undefined,
                                 done?: () => void, opts?: { ms?: number; fallbackMs?: number; beforeFly?: () => void }): boolean;
/** The instant exit for a box hidden at once: genieIn's picture, or a fresh one, poured into `to`. False out of Sliding. */
export declare function genieAway(box: HTMLElement | null | undefined, to: Element | DOMRect | null | undefined,
                                  opts?: { ms?: number; clip?: DOMRect | null }): boolean;
/** A picture already taken (a canvas), poured from `win` into `to` on its own canvas. False out of Sliding. */
export declare function genieShotAway(shot: HTMLCanvasElement | null | undefined, win: GenieRect | DOMRect | null | undefined,
                                      to: Element | DOMRect | GenieRect | null | undefined,
                                      opts?: { ms?: number; clip?: DOMRect | GenieRect | null; zIndex?: number; follow?: Element | null }): boolean;
/** A list's pour (ms), quicker than a window's. */
export declare const MENU_GENIE_MS: number;
/** A point as the 16x2 strip a menu pours out of and back into; null for no point. */
export declare const pointBox: (p: { x: number; y: number } | null | undefined) => GenieRect | null;
/** genieIn on a list's clock. */
export declare const menuIn: (box: HTMLElement | null | undefined, from: Element | DOMRect | GenieRect | null | undefined) => boolean;
export declare const MENU_GROW_CLASS: string;
/** menuIn, or in Transition a grow out of `from`, for a menu with no CSS entrance of its own; false leaves it to the dust. */
export declare const menuOpen: (box: HTMLElement | null | undefined, from: Element | DOMRect | GenieRect | null | undefined) => boolean;
/** A menu hidden at once: Sliding's pour into `to`, or Transition's shrinking copy; false leaves it to the dust. */
export declare const menuAway: (box: HTMLElement | null | undefined, to: Element | DOMRect | GenieRect | null | undefined) => boolean;
/** Ends `box`'s own transform transitions, so it is measured where it will stand. */
export declare function settleTransforms(box: Element | null | undefined): void;
