// What every mouse gesture does on the release: the compare divider, the rect-draw and
// zoom-rect bands, a point/segment/whole-line drag, and the end of a pan.
import type { DrawingApp } from './drawingApp.js';

/** The viewport frame a zoom-rect fits into, in CSS px. */
export interface AvailBox { w: number; h: number; }

/**
 * One release, dispatched to whichever gesture is live. `availBox` is called only by the
 * zoom-rect branch, since measuring the viewport forces a layout.
 */
export declare const releasePointer: (
  app: DrawingApp,
  e: MouseEvent,
  viewport: HTMLElement | null,
  availBox: () => AvailBox,
) => void;
