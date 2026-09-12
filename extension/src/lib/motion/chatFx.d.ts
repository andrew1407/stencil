export declare const MATERIALIZE_CLASS: string;
export declare const MATERIALIZE_VEIL_CLASS: string;
/** Reversed leaveThenRemove for a freshly-added row: it materializes out of dust in place. */
export declare function materialize(el: HTMLElement, opts?: { ms?: number; cols?: number; rows?: number }): Promise<void>;

export declare const CHAT_ENTER_MS: number;
export declare const CHAT_ENTERING_CLASS: string;
export declare function dustFitsScroller(el: HTMLElement, scroller?: Element | null): boolean;

export declare const CHAT_SLIDE_CLASS: string;
export declare const CHAT_SLIDE_MS: number;
/** Flies a chat entry in as dust (or the CSS slide entrance when particles are off). */
export declare function chatIn(
  el: HTMLElement, count?: number, index?: number, opts?: { host?: Element | null },
): Promise<void>;
