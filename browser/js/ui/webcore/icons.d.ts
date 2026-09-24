// The skin's pixel icons (config/iconsWebcore.json) and the live swap.
/** A 16-row palette map as `<rect>` runs; `ink` re-inks single palette characters. */
export declare const pixelRects: (rows: string[], ink?: Record<string, string> | null) => string;
/** The header mark's runs with its ring in `accent` (null = the map's own navy). */
export declare const pixelLogoRects: (accent: string | null, dark?: boolean) => string;
/** name → `<rect>` runs, built once per face (`dark` inks the dark palette). */
export declare const pixelTable: (dark?: boolean) => Record<string, string>;
/** One icon as a complete standalone `<svg>` (the favicon); `accent` inks the mark's ring. */
export declare const pixelIconSvg: (name: string, size?: number, accent?: string | null, dark?: boolean) => string;
/** Re-ink the header mark alone — what every accent change while the skin is on repaints. */
export declare const paintLogoMark: (root: ParentNode, accent: string | null, dark?: boolean) => void;
/** Install the skin table into ui/icons.js, or take it out. */
export declare const setPixelIcons: (on: boolean, dark?: boolean) => void;
/** Redraw every glyph under `root` in the art ui/icons.js serves now. */
export declare const swapIconArt: (root: ParentNode, on: boolean, accent?: string | null, dark?: boolean) => void;
