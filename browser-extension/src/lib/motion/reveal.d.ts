export declare const REVEAL_ITEM_CLASS: string;
export declare const REVEAL_IN_CLASS: string;
export declare const REVEAL_ENTERING_CLASS: string;
export declare const REVEAL_MASKED_CLASS: string;
export declare const REVEAL_ENTER_MS: number;

/** 0 while wholly on screen, rising with the clipped share, 1 once [top,bottom) is gone from `viewH`. */
export declare function revealDissolve(top: number, bottom: number, viewH: number): number;
export declare function revealGrain(top: number, bottom: number, viewH: number): number;
/** Watches `root` and ramps every child matching `selector` as it scrolls. Returns a disconnect fn. */
export declare function observeReveal(root: Element, selector: string): () => void;
