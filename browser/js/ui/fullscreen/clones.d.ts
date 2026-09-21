/** Rebuild the fullscreen controls strip from the live `#controls-body .controls`. */
export declare function populateFsControls(fsControlsPanel: HTMLElement): void;

/** Relay every interaction on a cloned control to its original, and mirror back. */
export declare function bindClonedControls(cloneRoot: HTMLElement, srcRoot: HTMLElement): void;

/** Rebuild the fullscreen points list from the live `#coord-panel`, with re-prefixed ids. */
export declare function populateFsPoints(fsPointsPanel: HTMLElement): void;
