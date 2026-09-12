/** Class marking a `.ctrl-sep` whose two neighbours ended up on different wrapped rows. */
export declare const WRAPPED_SEP_CLASS: string;

/** Hide/show each `.ctrl-sep` under `root` to match the wrapping flex layout, settling over a few passes. */
export declare function syncWrappedSeparators(root: Element | null, passes?: number): void;
