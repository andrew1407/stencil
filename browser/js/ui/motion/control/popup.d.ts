// A popup's entrance and exit, the dust first, else the mode's own pour or grow.
export interface StagePoint { x: number; y: number }
/** Forms `menu` out of `point` (the dust), or pours/grows it out of the box round it; true when a flight plays. */
export declare const popupIn: (menu: HTMLElement, point: StagePoint | null | undefined, ms: number) => boolean;
/** Pours/shrinks `menu` back into the box round `point`, or scatters it there; true when a flight plays. */
export declare const popupOut: (menu: HTMLElement, point: StagePoint | null | undefined, ms: number) => boolean;
