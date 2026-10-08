import type { IconDragHandle, IconDragHooks } from './iconDrag.js';
import type { HoldZoom } from '../bindings/viewport/holdZoom.js';
import type { ZoomPan } from '../../core/zoom/pan.js';

/** k in z0·e^(±k·d): 300 px of drag spans ×8 or ÷8. */
export declare const ZOOM_DRAG_K: number;

/** z0·e^(sign·k·d), through `clamp` (the zoom limits). */
export declare function zoomAtDistance(
  z0: number, d: number, sign: number, clamp?: (s: number) => number, k?: number,
): number;

/** A zoom button's drag: the zoom follows the distance from the button; over it (the cancel) it is z0. */
export declare function zoomDragHooks(opts: {
  btn: Element;
  sign: number;
  zp: ZoomPan;
  hold?: HoldZoom | null;
}): Required<IconDragHooks>;

export interface FitStepper {
  /** +1 over zoom-in, −1 over zoom-out, 0 over neither. */
  over(sign: number): void;
  stop(): void;
  readonly sign: number;
}

/** Steps at once on entering + or −, then every `repeatMs` until the pointer leaves it. */
export declare function createFitStepper(opts: {
  step(sign: number): void;
  repeatMs?: number;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (id: unknown) => void;
}): FitStepper;

/** The fit button's drag: − and + glow as targets and step while under the pointer; fit cancels. */
export declare function fitDragHooks(opts: {
  zp: ZoomPan;
  zoomIn: Element | null;
  zoomOut: Element | null;
  stepper?: FitStepper;
}): Required<IconDragHooks>;

export declare function wireZoomDrag(
  btn: HTMLElement | null, sign: number, zp: ZoomPan, hold?: HoldZoom | null,
): IconDragHandle | null;

export declare function wireFitDrag(
  btn: HTMLElement | null, zp: ZoomPan, buttons: { zoomIn: Element | null; zoomOut: Element | null },
): IconDragHandle | null;
