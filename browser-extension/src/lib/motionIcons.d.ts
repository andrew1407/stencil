export interface MotionIconMap { [mode: string]: string; }

export declare const NONE_LINE_LEN: number;
export declare const TRANSITION_SHAFT_LEN: number;
/** The progress the 'slide' glyph rests at (0 = the whole window, 1 = in the dock), its outline there, and the
 * hover's evenly timed keyframes from the dock to that rest, on the window flight's own curve. */
export declare const GENIE_GLYPH_REST_P: number;
export declare const GENIE_GLYPH_STEPS: number;
export declare const genieGlyphPath: (p: number) => string;
export declare const genieGlyphKeyframes: () => Array<{ at: number; d: string }>;
export declare const MOTION_ICONS: MotionIconMap;
export declare function motionModeIcon(mode: string): string;
