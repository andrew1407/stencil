export declare const MOTION_SETTLED_CLASS: string;
/** Keeps an open panel's entrance from replaying when the motion mode changes; returns its teardown. */
export declare function settleOnMotionChange(host: HTMLElement | null | undefined): () => void;
