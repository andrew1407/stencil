import type { Point } from '../../core/geometry.js';
export const SUB_EDGE_PAD: number;
export const SUB_MIN_INSET: number;
export const SUB_GAP: number;

/** A measured box, as getBoundingClientRect() reports it. */
export interface RectLike { left: number; right: number; top: number; }

/** Viewport-clamped position for a flyout of size sw×sh beside the row `ir`. */
export function submenuPlacement(ir: RectLike, sw: number, sh: number, vw: number, vh: number):
  { left: number; top: number };

export function samePoint(a: Point | null, b: Point | null): boolean;
