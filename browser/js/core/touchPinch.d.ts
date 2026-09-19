// The two-finger pinch: the anchor taken at the start, the scale each move stashes, the one
// coalesced frame that writes it, and the end that hands the level back to ZoomPan.
import type { DrawingApp } from './drawingApp.js';

/** The live pinch, anchored on the image point under the starting midpoint. */
export interface PinchSession {
  mode: 'pinch';
  startDist: number;
  startScale: number;
  imgX: number;
  imgY: number;
  vpLeft: number;
  vpTop: number;
  pending: { scale: number; midX: number; midY: number } | null;
  raf: number | null;
}

export declare const beginPinch: (app: DrawingApp, viewport: HTMLElement,
  a: Touch, b: Touch) => PinchSession;
/** Stashes the clamped scale and midpoint; the rAF writes them. */
export declare const pinchTo: (app: DrawingApp, st: PinchSession, a: Touch, b: Touch) => void;
export declare const applyPinchFrame: (app: DrawingApp, viewport: HTMLElement, st: PinchSession) => void;
/** Lands the pending frame first, so the span the fingers held is the level persisted. */
export declare const endPinch: (app: DrawingApp, st: PinchSession, applyNow: () => void) => void;
