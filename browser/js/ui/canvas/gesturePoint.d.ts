/** Side length of the small anchor box built around a recorded gesture point. */
export declare const GESTURE_ANCHOR_PX: number;

/** A small client-rect around the last press, or the focused element's rect, or null. */
export declare const gestureAnchorRect: () => { left: number; top: number; right: number; bottom: number; width: number; height: number } | null;
