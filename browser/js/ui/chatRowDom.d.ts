/** Rewrites className, keeping the motion classes motion.js owns across a repaint. */
export declare const setRowClass: (el: Element, cls: string) => void;

/** The row's one dedicated text child, created on first use. */
export declare const rowTextNode: (el: Element) => Element;

/** The max-width a wrapped bubble should hug; null for a single line. */
export declare const shrinkWrapWidth: (lineWidths: readonly number[] | null | undefined) => number | null;

export declare const applyShrinkWrap: (el: HTMLElement | null | undefined) => void;

/** Re-measures every settled row's shrink-wrap when the transcript's box changes. */
export declare const bindShrinkWrapResize: (transcript: HTMLElement & { _shrinkWrapBound?: boolean }) => void;
