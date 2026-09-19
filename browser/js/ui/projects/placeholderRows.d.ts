/** A grey stand-in row, shown while the server listing is still in flight. */
export declare const makeSkeletonRow: () => HTMLDivElement;

/** A read-only row for an incognito session open in another tab. */
export declare const makeIncognitoPeerRow: (p: { name?: string; peerId?: string }) => HTMLDivElement;

/** The caption when the current filter lists nothing at all. */
export declare const emptyLabelFor: (mode: string) => string;
