// The accent the page shows right now, a hover preview included.
/** '#rrggbb' of the shown accent: the inline custom --accent, else the data-accent preset. */
export declare const shownAccentHex: (doc?: Document) => string;
/** Calls `onShown(hex)` whenever the shown accent changes (a pick, a preview or its end); returns the unwatch. */
export declare const watchShownAccent: (doc: Document, onShown: (hex: string) => void) => () => void;
