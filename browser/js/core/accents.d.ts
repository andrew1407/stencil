// Accent (brand-colour) presets and the contrast rule that picks the ink painted on one.
// The palette is config/accents.json — canonical for the extension and desktop mirrors —
// and the contrast maths is pure, so every surface answers "dark or light glyph?" alike.

/** One palette row: the key stored in localStorage, its label, and the primary hex. */
export interface AccentPreset { key: string; label: string; hex: string; }

export declare const ACCENTS: AccentPreset[];
export declare const ACCENT_STORAGE_KEY: 'drawingApp_accent';
export declare const DEFAULT_ACCENT: 'violet';
export declare const isAccent: (key: unknown) => boolean;
/** Primary hex for an accent key, falling back to the first preset (violet). */
export declare const accentHex: (key: string) => string;
/** The app favicon as an SVG string with the panel outline painted in `hex`. */
export declare const faviconSvg: (hex: string) => string;
/** '#rgb' or '#rrggbb' (leading '#' optional) → '#rrggbb' lower-case, else null. */
export declare const normalizeHex: (value: unknown) => string | null;
/** WCAG relative luminance (0..1) of a hex colour, or null when it isn't one. */
export declare const relativeLuminance: (hex: unknown) => number | null;
/** Contrast ratio (1..21) of pure white / pure black against `hex`; null when not a hex. */
export declare const contrastWithWhite: (hex: unknown) => number | null;
export declare const contrastWithBlack: (hex: unknown) => number | null;
export declare const ON_ACCENT_LIGHT: '#ffffff';
export declare const ON_ACCENT_DARK: '#1a1a1a';
/** True when black reads better on `hex` than white does; a non-hex answers false. */
export declare const needsDarkGlyph: (hex: unknown) => boolean;
export declare const onAccentInk: (hex: unknown) => '#ffffff' | '#1a1a1a';
/** The preset keys that take the dark ink, derived from the palette. */
export declare const LIGHT_ACCENT_KEYS: string[];
/** Any CSS colour → '#rrggbb' via a canvas probe; null/'transparent'/unparseable pass through. */
export declare const toHexColor: <T>(v: T) => T | string;
/** Repaint the tab favicon and the PWA theme-color to a literal hex; no-op without a DOM. */
export declare const applyFaviconHex: (hex: string) => void;
export declare const applyAccentFavicon: (key: string) => void;
