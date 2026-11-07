// A hover tip's clocks and flights, and the fold and dust helpers the control areas share.
export interface StagePoint { x: number; y: number }
export interface ClientBox { left: number; top: number; width: number; height: number }
export declare const TIP_DUST_IN_MS: number;
export declare const TIP_DUST_OUT_MS: number;
/** One wake delay across surfaces (ms). */
export declare const TIP_SHOW_DELAY_MS: number;
export declare const TIP_GROW_CLASS: string;
export declare const TIP_SHRINK_CLASS: string;
/** Transition's grow out of nothing; false under any other mode. */
export declare const tipGrowIn: (el: HTMLElement | null | undefined) => boolean;
/** Sliding's pour or Transition's grow out of `point`; its run in ms, 0 (or false) where neither plays. `landed` runs as it lands. */
export declare const tipFallbackIn: (el: HTMLElement | null | undefined, point: StagePoint | null | undefined,
  landed?: (() => void) | null) => number | false;
/** The matching exit, called while the tip is still shown; false where no flight plays. */
export declare const tipFallbackOut: (el: HTMLElement | null | undefined, point: StagePoint | null | undefined) => boolean;
/** Transition's exit for a tip already gone: a copy shrinks where it stood; false under any other mode. */
export declare const tipCopyAway: (el: HTMLElement | null | undefined) => boolean;
/** The centre of an element (or rect); null for a detached owner. */
export declare const rectCenter: (elOrRect: Element | ClientBox | null | undefined) => StagePoint | null;
/** The box `el` has with `cls` toggled `on` under `scope`, measured with transitions off (`instant`); null when too small. */
export declare function foldBox(el: Element, scope: Element, cls: string, on: boolean, instant: string): ClientBox | null;
export declare const FOLD_INSTANT_CLASS: string;
export declare const FOLD_DUST_OUT_MS: number;
export declare const CONTROLS_DUST_IN_MS: number;
export declare const CONTROLS_DUST_OUT_MS: number;
/** What foldDust answers when Sliding poured the fold instead of flying dust. */
export declare const FOLD_POURED: 'poured';
/** The fold's flight: the box the cloud flew over, null when none did, or FOLD_POURED. */
export declare function foldDust(el: HTMLElement, scope: Element, cls: string, hiding: boolean, dock: string,
  opts?: { inMs?: number; outMs?: number; toggle?: (() => void) | null; picture?: boolean; box?: ClientBox | null;
    edge?: boolean; home?: Element | ClientBox | null; pourMs?: number }): ClientBox | null | 'poured';
/** Closing, the fold's space waits this share of the flight before it moves. */
export declare const SPACE_CLOSE_WAIT: number;
/** The flight outlasts the slide by this (ms), so it covers the landing. */
export declare const SPACE_TAIL_MS: number;
/** A hover popup's dust in and out of its host. */
export declare function wireHoverDust(host: Element | null | undefined, popup: HTMLElement | null | undefined,
  opts?: { inMs?: number; outMs?: number }): void;
