// The tab's favicon and theme-color in the accent, over core/settings/accents.js's favicon art.

/** Repaint the tab favicon and the PWA theme-color to a literal hex; no-op without a DOM. */
export declare const applyFaviconHex: (hex: string) => void;
/** The preset's hex, or the default accent's for an unknown key. */
export declare const applyAccentFavicon: (key: string) => void;
