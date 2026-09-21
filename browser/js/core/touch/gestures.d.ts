// Touch gestures: pure classification + pinch math for DrawingApp's touch wiring. All
// coordinates are client/screen px, so tolerances feel the same at any zoom.

export interface TouchThresholds { moveTol: number; tapMaxMs: number; longPressMs: number; }
export declare const TOUCH_DEFAULTS: TouchThresholds;
export declare const dist: (ax: number, ay: number, bx: number, by: number) => number;
export interface TouchPoint { clientX: number; clientY: number; }
export declare const midpoint: (t0: TouchPoint, t1: TouchPoint) => { x: number; y: number };
export declare const touchDist: (t0: TouchPoint, t1: TouchPoint) => number;
export interface PressSummary { moved: number; elapsed: number; }
export declare const classifyEnd: (press: PressSummary, opts?: Partial<TouchThresholds>) => 'tap' | 'drag';
export declare const isLongPress: (press: PressSummary, opts?: Partial<TouchThresholds>) => boolean;
