export declare const ATTACH_SETTLE_STEP_MS: number;
export declare const ATTACH_SETTLE_TRIES: number;

/** `count` rows leaving at once share one dust-tile budget (motion.js scatterGridFor). */
export declare const chatLeave: (el: Element, done: () => void, count?: number, index?: number) => void;
/** Chips ride the longer chip clock (css chipLeave hold + collapse). */
export declare const chipLeave: (el: Element, done: () => void) => void;
