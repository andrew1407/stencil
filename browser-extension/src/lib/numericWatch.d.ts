// One page-wide observer that upgrades every numeric field, the ones rendered later included.

/** Enhance the fields on the page now and every one added under `root` later. */
export declare function watchNumericInputs(root?: Node): MutationObserver;
