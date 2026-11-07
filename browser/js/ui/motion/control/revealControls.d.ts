// A control group or bar revealed and hidden: the slot's slide, the dust or the mode's own flight.
export declare const REVEAL_GROUP_IN_MS: number;
export declare const REVEAL_GROUP_OUT_MS: number;
export declare const BAR_CLOSING_CLASS: string;
export declare const BAR_HELD_CLASS: string;
/** Closes a bar's slot over `ms` from its held height; false when it hid at once. */
export declare const closeBarSlot: (el: HTMLElement, ms: number, opts?: { setTimer?: typeof setTimeout }) => boolean;
/** Shows the bar at once while `want()` holds, or closes its slot after its controls leave; true when it just appeared. */
export declare const revealBar: (el: HTMLElement | null | undefined, want: () => boolean,
  opts?: { display?: string; ms?: number; setTimer?: typeof setTimeout }) => boolean;
/** Reveal or hide a group; true when a flight (dust, slide or grow) played. */
export declare function revealControls(el: HTMLElement | null | undefined, show: boolean, display?: string,
  opts?: { vertical?: boolean | null; dust?: boolean; ms?: number }): boolean;
/** Swap a control's text between two mark flights; `apply` runs between them. */
export declare function markSwap(el: HTMLElement, apply: () => void,
  opts?: { ms?: number; outMs?: number; paint?: unknown }): boolean;
