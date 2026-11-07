// Rows and chips arriving and leaving: the glow, the collapse, the filter dust and the list hold.
export interface StagePoint { x: number; y: number }
export interface ClientBox { left: number; top: number; width: number; height: number }
export declare const ARRIVE_GLOW_CLASS: string;
export declare const ARRIVE_ACTIVE_CLASS: string;
export declare const LANDING_CLASS: string;
/** A `size` square centred on `point`: the box an arrival flies out of. */
export declare const arrivalBox: (point: StagePoint, size?: number) => ClientBox;
/** FLIP `el` in from `point`, or flash its landing without one. */
export declare function arriveFrom(el: HTMLElement | null | undefined, point?: StagePoint | null, opts?: { ms?: number }): void;
export declare const LEAVE_MS: number;
export declare const CHAT_LEAVE_MS: number;
export declare const CHIP_LEAVE_MS: number;
export declare const LEAVING_CLASS: string;
/** The length (ms) of a row's whole flight under Sliding and Transition. */
export declare const MODE_ROW_MS: number;
/** True under the two modes whose rows fly whole rather than scatter. */
export declare const shouldRowFly: () => boolean;
/** `ms`, or the mode's own row clock when that is longer. */
export declare const rowClock: (ms: number) => number;
export declare const LEAVE_HOLD_CLASS: string;
/** Collapse and scatter `el`, then run `done`; resolves as the flight ends. */
export declare function leaveThenRemove(el: HTMLElement | null | undefined, done?: () => void,
  opts?: { ms?: number; cols?: number; rows?: number; dustMs?: number; drift?: number; px?: number; hold?: boolean }): Promise<void>;
/** How long a removal's wipe runs (ms), 0 without dust. */
export declare const wipeDurationMs: (dustMs?: number) => number;
/** A removal-in-flight hold: `begin()` answers a settle that waits out the wipe; `holding` while any is pending. */
export declare const createListHold: (opts?: { settle?: () => void; wait?: () => number; setTimer?: typeof setTimeout }) =>
  { begin: () => () => Promise<void>; readonly holding: boolean };
export declare const FILTER_ENTER_MS: number;
export declare const FILTER_ENTERING_CLASS: string;
export declare const FILTER_DUST_MS: number;
export declare const ROW_ARRIVE_MS: number;
export declare const ROW_ARRIVE_DELAY_MS: number;
export declare const FILTER_DUST_DRIFT: number;
/** The `index`th of `count` rows forming from motes; false where none fly. */
export declare const filterDust: (el: HTMLElement | null | undefined, index?: number, count?: number, box?: ClientBox | null) => boolean;
/** The matching scatter away. */
export declare const filterDustOut: (el: HTMLElement | null | undefined, index?: number, count?: number, box?: ClientBox | null) => boolean;
/** The keys that enter and leave between two renders. */
export declare const filterDelta: (before?: readonly string[], after?: readonly string[]) => { entering: string[]; leaving: string[]; moved: boolean };
/** A list's filter animator over its keys, renderer and row finder. */
export declare const createFilterAnimator: (opts?: {
  keys?: () => string[]; next?: () => string[]; render?: () => void; find?: (key: string) => HTMLElement | null;
  enterMs?: number; max?: number; setTimer?: typeof setTimeout; reduced?: () => boolean; leave?: boolean;
  dustOut?: typeof filterDustOut;
}) => unknown;
/** True when the empty notice shows: nothing listed and no removal in flight. */
export declare const emptyStateVisible: (count: number, holding?: boolean) => boolean;
/** Wear `cls` for `ms` (or until animationend), then drop it. */
export declare function flashLanding(el: HTMLElement | null | undefined, cls?: string, ms?: number): void;
