// A picture arriving or leaving under Sliding (the genie) or Transition (a rise and a sink).
export declare const RISE_CLASS: string;
export declare const SINK_CLASS: string;
/** The picture's pour-in length at the default speed (ms): 1.5x a window's open. */
export declare const CANVAS_ARRIVE_MS: number;
/** The strip a picture pours out of and back into: the middle of `vp`'s bottom edge. */
export declare const bottomStrip: (vp: Element | null | undefined) => { left: number; top: number; width: number; height: number } | null;
/** Scroll the page so `vp` is on screen, at once, before a flight measures it. */
export declare const showView: (vp: Element | null | undefined) => void;
/** Play the arrival; false when neither mode is on, leaving the caller's own landing. */
export declare function slideCanvasIn(box: HTMLElement | null, vp: Element | null, from?: { x: number; y: number } | null): boolean;
/** Play the exit from a copy of the picture; call before the canvas is cleared. */
export declare function slideCanvasOut(box: HTMLElement | null, vp: Element | null): boolean;
/** The same exit, found from the canvas itself. */
export declare const slideCanvasAway: (canvas: Element | null | undefined) => boolean;
