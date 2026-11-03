/** The dashed diagonal's length in the 'none' glyph. */
export declare const NONE_LINE_LEN: number;
/** The arrow shaft's length in the 'transition' glyph. */
export declare const TRANSITION_SHAFT_LEN: number;
/** The progress the 'slide' glyph rests at (0 = the whole window, 1 = in the dock), its outline there, and the
 * hover's evenly timed keyframes from the dock to that rest, on the window flight's own curve. */
export declare const GENIE_GLYPH_REST_P: number;
export declare const GENIE_GLYPH_STEPS: number;
export declare const genieGlyphPath: (p: number) => string;
export declare const genieGlyphKeyframes: () => Array<{ at: number; d: string }>;
/** prefs.js mode key → its inline-SVG glyph. */
export declare const MOTION_ICONS: Record<string, string>;

/** The icon for a motion mode, or '' for an unknown/stale key. */
export declare const motionModeIcon: (mode: string) => string;
