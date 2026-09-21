/** The dashed diagonal's length in the 'none' glyph. */
export declare const NONE_LINE_LEN: number;
/** The arrow shaft's length in the 'slide' glyph. */
export declare const SLIDE_SHAFT_LEN: number;
/** prefs.js mode key → its inline-SVG glyph. */
export declare const MOTION_ICONS: Record<string, string>;

/** The icon for a motion mode, or '' for an unknown/stale key. */
export declare const motionModeIcon: (mode: string) => string;
