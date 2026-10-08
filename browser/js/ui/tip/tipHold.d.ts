/** True while a control drag holds every tooltip off. */
export declare const tipsHeld: () => boolean;

/** `fn` runs as each hold begins (its tooltip drops what it shows); the return unregisters it. */
export declare const onTipsHeld: (fn: () => void) => () => boolean;

/** Starts (true) or ends (false) the hold; starting runs every watcher once. */
export declare const holdTips: (on: boolean) => void;
