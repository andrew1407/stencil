import type { DrawingApp } from '../core/drawingApp.js';

/** The Blank tab: the fill colour with its presets, the pixel size, and Create. */
export declare function createBlankTab(args: {
  app: DrawingApp;
  els: {
    colorEl: HTMLInputElement;
    colorHexEl: HTMLElement | null;
    widthEl: HTMLInputElement;
    heightEl: HTMLInputElement;
    createBtn: HTMLElement;
    whiteBtn: HTMLElement;
    blackBtn: HTMLElement;
  };
  pageDims: () => { width: number; height: number };
  /** The chosen server, or null for a local project. */
  target: () => string | null;
  close: () => void;
}): { reset: () => void };
