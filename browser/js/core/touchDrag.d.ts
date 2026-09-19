// Single-finger direct manipulation: what a finger grabs where it lands, how that grab
// follows it, what a release does, and the synthetic click a stationary tap becomes.
import type { DrawingApp } from './drawingApp.js';

/** The single-finger gesture a press became, and what the move/end paths need of it. */
export interface TouchSession {
  mode: 'tap' | 'point' | 'segment' | 'done';
  id: number;
  startX?: number;
  startY?: number;
  startT?: number;
  moved?: number;
  dragged?: boolean;
}

export declare const findTouchById: (touches: TouchList, id: number) => Touch | null;
/** Takes the grab and returns its mode, or null for empty space. */
export declare const grabTouchTarget: (app: DrawingApp, t: Touch) => 'point' | 'segment' | null;
export declare const touchDragTo: (app: DrawingApp, st: TouchSession, t: Touch) => void;
export declare const touchDragEnd: (app: DrawingApp, st: TouchSession) => void;
/** Lifted without moving: drop the grab so the press falls through as a tap. */
export declare const releaseTouchGrab: (app: DrawingApp, st: TouchSession) => void;
/** A modifier-free left click at the lifted finger, or the press point. */
export declare const tapClickAt: (app: DrawingApp, e: TouchEvent, st: TouchSession) => void;
