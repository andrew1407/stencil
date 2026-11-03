/** Marks a surface's stand-in copy; `dd-leaving` plays Transition's exit on it. */
export declare const LEAVE_COPY_CLASS: string;
export declare const LEAVING_CLASS: string;
/** A copy of `box` fixed over its client rect, ids dropped; null when it shows nothing. */
export declare function holdCopy(box: HTMLElement | null | undefined): HTMLElement | null;
/** Transition's exit for a box its caller hides at once; false when there is nothing to show. */
export declare function cssAway(box: HTMLElement | null | undefined): boolean;
