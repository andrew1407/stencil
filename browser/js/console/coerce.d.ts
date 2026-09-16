/** null/undefined become ''; everything else coerces via String(). */
export declare const str: (v: unknown) => string;

/** One keyword per array entry; a string splits on commas and newlines, never on spaces. */
export declare const splitKeywords: (v: unknown) => string[];
