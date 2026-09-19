import type { DrawingApp } from '../../core/drawingApp.js';

/** Builds the list's one hidden colour field; the result opens it beside a row's swatch. */
export declare function createColorPicker(deps: {
  app: DrawingApp;
  list: HTMLElement;
  render: () => void;
}): (meta: { id: string; color?: string }, btn: HTMLElement) => void;
