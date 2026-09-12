import type { Point } from './surfaceMotion.js';

/** Drop whatever `el` has in flight (cloud and driving classes), leaving the end state untouched. */
export declare function settleSurface(el: Element | null | undefined): void;
export declare function surfaceIn(el: Element | null | undefined, point: Point, opts?: { ms?: number }): boolean;
export declare function surfaceOut(el: Element | null | undefined, point: Point, opts?: { ms?: number }): boolean;
