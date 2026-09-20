// The logo stage's painter: backdrop, glow, spoke ring and the mark.
import type { DrawingApp } from '../core/drawingApp.js';

/** 0 → 1 → 0 over the beat clock. */
export declare const beatAt: (tMs: number, ms?: number) => number;
/** The ring's angle in radians at `tMs`. */
export declare const spinAt: (tMs: number, ms?: number) => number;

/** The app mark as an image, its frame painted in `hex`. */
export declare const stageMark: (doc: Document, hex: string) => HTMLImageElement;
/** The accent the mark and its light are painted in: the custom hex, else the preset's. */
export declare const markHex: (app: DrawingApp | null) => string;

export interface MarkPose {
  x: number;
  y: number;
  size: number;
  hex: string;
  beat: number;
  boost: number;
  angle?: number;
}
export declare const paintBackdrop: (ctx: CanvasRenderingContext2D, w: number, h: number,
  fade: number) => void;
export declare const paintGlow: (ctx: CanvasRenderingContext2D, pose: MarkPose) => void;
export declare const paintSpokes: (ctx: CanvasRenderingContext2D, pose: MarkPose & { angle: number }) => void;
export declare const paintMark: (ctx: CanvasRenderingContext2D, img: HTMLImageElement | null,
  pose: { x: number; y: number; size: number }) => void;
