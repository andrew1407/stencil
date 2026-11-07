// A chat entry's arrival: out of its motes, or slid or grown in under the other modes.
export interface StagePoint { x: number; y: number }
export interface ClientBox { left: number; top: number; width: number; height: number }
export declare const MATERIALIZE_CLASS: string;
export declare const MATERIALIZE_VEIL_CLASS: string;
/** The row forms from motes and unveils as they land; resolves once it stands. */
export declare function materialize(el: HTMLElement | null | undefined,
  opts?: { ms?: number; cols?: number; rows?: number; dustMs?: number; drift?: number; px?: number; inPlace?: boolean }): Promise<void>;
export declare const CHAT_ENTER_MS: number;
export declare const CHAT_ENTERING_CLASS: string;
export declare const CHAT_ENTER_REACH: number;
/** The point an entry's motes gather from, past its tail. */
export declare const chatArrivalPoint: (el: Element | null | undefined, r?: DOMRect | null, s?: DOMRect | null) => StagePoint | null;
/** True when the cloud fits inside the transcript's scroller. */
export declare const dustFitsScroller: (el: Element | null | undefined, scroller?: Element | null, r?: DOMRect | null, s?: DOMRect | null) => boolean;
export declare const CHAT_SLIDE_CLASS: string;
export declare const CHAT_SLIDE_MS: number;
export declare const CHAT_GROW_CLASS: string;
export declare const CHAT_SIDE_CLASS: string;
export declare const CHAT_SIDE_MS: number;
/** The strip at the entry's tail that Sliding pours it out of. */
export declare const chatTailHome: (el: Element | null | undefined, r?: DOMRect | null, view?: DOMRect | null) => ClientBox | null;
export declare const CHAT_SURFACE_IN_MS: number;
export declare const CHAT_SURFACE_OUT_MS: number;
/** The entry's arrival, the `index`th of `count`; resolves once it stands. */
export declare function chatIn(el: HTMLElement | null | undefined, count?: number, index?: number): Promise<void>;
