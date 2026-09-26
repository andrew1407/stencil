/** Side length of the small anchor box built around a recorded gesture point. */
export declare const GESTURE_ANCHOR_PX: number;

/** Record that a shortcut ran: `id` is its hotkeysConfig entry, whose control a confirm grows from. */
export declare const noteKeyGesture: (id: string | null) => void;

/** After a shortcut, its control's rect (null when folded away); else a box around the last press, the focused element's rect, or null. */
export declare const gestureAnchorRect: () => { left: number; top: number; right: number; bottom: number; width: number; height: number } | null;
