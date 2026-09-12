export type MotionMode = 'particles' | 'water' | 'fire' | 'slide' | 'none';
export interface MotionPrefs { mode: MotionMode; drawing: boolean; }

export declare const MOTION_STORAGE_KEY: string;
/** Fired after every change (bus/appBus.js EVENTS.motionChanged). */
export declare const MOTION_EVENT: string;
export declare const MOTION_MODES: MotionMode[];
/** The motion modes that paint through dustCloud.js styleFrame. */
export declare const PARTICLE_MODES: MotionMode[];
export declare const DEFAULT_MOTION_MODE: MotionMode;
export declare const DEFAULT_DRAWING_ANIMATIONS: boolean;
/** [modeKey, label] pairs for the desktop combo and the extension options page. */
export declare const MOTION_MODE_LABELS: [MotionMode, string][];

/** An unrecognized value falls back to DEFAULT_MOTION_MODE. */
export declare const normalizeMotionMode: (v: unknown) => MotionMode;

export declare const motionPrefs: () => MotionPrefs;
export declare const motionMode: () => MotionMode;
export declare const drawingAnimations: () => boolean;
/** Reads prefers-reduced-motion live, at call time. */
export declare const prefersReducedMotion: () => boolean;
/** True when mode is 'none' or the OS prefers reduced motion. */
export declare const motionReduced: () => boolean;
/** False in 'slide'/'none': the surface's own CSS entrance is in charge instead. */
export declare const dustEnabled: () => boolean;
/** 'dust' | 'water' | 'fire', or null when no particles fly. */
export declare const particleStyle: () => string | null;
export declare const drawMotionEnabled: () => boolean;
/** Mirrors what prePaintTheme.js writes before first paint (data-motion on <html>). */
export declare const applyMotionAttr: (root?: Element | null) => void;
/** Persist, restamp <html>, and publish MOTION_EVENT. */
export declare const setMotionPrefs: (patch?: Partial<MotionPrefs>) => MotionPrefs;
/** Tests only: forget what was loaded and read the store again. */
export declare const reloadMotionPrefs: () => MotionPrefs;
