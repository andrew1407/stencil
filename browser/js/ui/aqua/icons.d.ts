// The aqua skin's icons (config/iconsAqua.json).
export declare const AQUA_ICONS: { size: number; icons: Record<string, string> };
/** One glyph's inner markup with gradient ids of its own; '' for an unknown name. */
export declare const aquaIconArt: (name: string) => string;
/** Serve the aqua art from ui/icons.js (on) or hand it back to the line-art (off). */
export declare const setAquaIcons: (on: boolean) => void;
