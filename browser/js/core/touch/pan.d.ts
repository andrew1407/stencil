// One-finger pan: a press on empty canvas that wanders past the tap tolerance scrolls the
// viewport under the finger 1:1, the touch twin of the mouse's Alt+drag.
import type { DrawingApp } from '../drawingApp.js';
import type { TouchSession } from './drag.js';

/** The single-finger session once it became a pan. */
export interface TouchPanSession extends Omit<TouchSession, 'mode'> {
  mode: 'pan';
  lastX: number;
  lastY: number;
}

/** Turns a tap session into a pan anchored at the press point. */
export declare const beginTouchPan: (app: DrawingApp, st: TouchSession | TouchPanSession) => void;
/** Scrolls by the finger's step since the last move, so the image follows it. */
export declare const touchPanTo: (viewport: HTMLElement, st: TouchPanSession, t: Touch) => void;
export declare const endTouchPan: (app: DrawingApp) => void;
