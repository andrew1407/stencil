/** True while the document holds the OS fullscreen. */
export declare const isScreenFull: () => boolean;
/** Requests OS fullscreen on the document; false when the page may not. */
export declare const enterScreen: () => Promise<boolean>;
/** The browser's reason the last enterScreen() was refused; '' when it was not. */
export declare const screenRefusal: () => string;
/** Leaves OS fullscreen; a no-op when not in it. */
export declare const exitScreen: () => Promise<void>;
/** Calls `cb(isFull)` on every `fullscreenchange`; returns the unsubscribe. */
export declare const onScreenChange: (cb: (isFull: boolean) => void) => () => void;
