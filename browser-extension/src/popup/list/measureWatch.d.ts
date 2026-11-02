// Shapes for popup/list/measureWatch.js — the image list's lazy size probe and its observer bookkeeping.
import type { PopupImage } from './model.js';

export interface WatchObserver {
  observe(el: Element): void;
  unobserve(el: Element): void;
}

export interface MeasureWatchOptions {
  /** Builds the observer that reports rows nearing the viewport, e.g. an IntersectionObserver. */
  makeObserver: (onEntries: (entries: { target: Element; isIntersecting: boolean }[]) => void) => WatchObserver;
  /** Measures `image`; `done` runs once its probe has loaded or failed. */
  measure: (image: PopupImage, li: Element, done: () => void) => void;
}

export interface MeasureWatch {
  /** Watches `li` until it nears the viewport; a measured or in-flight image is skipped. */
  watch(li: Element, image: PopupImage): void;
  forget(li: Element): void;
  /** Lets go of every watched row that is no longer in the document. */
  sweep(): void;
  /** Rows watched now. */
  readonly size: number;
}

export declare const createMeasureWatch: (opts: MeasureWatchOptions) => MeasureWatch;
