/** One passive resize listener shared by every subscriber; returns an unsubscribe. */
export declare const onWindowResize: (fn: () => void) => () => void;

/** Coalesces a burst of calls into one on the next frame, with the newest arguments. */
export declare const perFrame: <A extends unknown[]>(fn: (...args: A) => void) => (...args: A) => void;
