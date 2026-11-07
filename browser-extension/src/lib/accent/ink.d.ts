// The accent's pure colour rules (browser twin: js/core/settings/accents.js).
/** `value` as lower-case `#rrggbb`, or null when it is no hex colour. */
export declare const normalizeHex: (value: unknown) => string | null;
/** WCAG relative luminance of a hex colour, or null when it isn't one. */
export declare const relativeLuminance: (hex: string) => number | null;
export declare const ON_ACCENT_LIGHT: string;
export declare const ON_ACCENT_DARK: string;
/** True when near-black ink contrasts more on `hex` than white does. */
export declare const needsDarkGlyph: (hex: string) => boolean;
/** The ink that reads on `hex`: white or near-black. */
export declare const onAccentInk: (hex: string) => string;
