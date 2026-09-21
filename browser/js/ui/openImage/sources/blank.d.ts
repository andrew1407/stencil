// Shape of the Blank tab (sources/blank.js): the element and what Create asks it for.
import { StencilElement } from '../../base.js';

/** What `create-blank` reports: the canvas to make. */
export interface CreateBlankDetail {
  color: string;
  width: number;
  height: number;
}

export declare class StencilOiBlankTab extends StencilElement {
  static inner(): string;
  static template(): string;
  /** White, at the project page's own pixel size. */
  reset(pageDims: { width: number; height: number }): void;
  /** Validates 1–8192 px, then emits `create-blank`; notifies and stays put if not. */
  requestCreate(): void;
}
