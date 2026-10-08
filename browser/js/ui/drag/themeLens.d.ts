// Dragging the theme switch: a lens on the pointer previews the page in the other theme; the drag
// ending anywhere, or Escape, closes it and changes nothing.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { IconDragHandle, IconDragHooks } from './iconDrag.js';

export declare const LENS_RADIUS_PX: number;
export declare const LENS_CLASS: string;
export declare const RIM_CLASS: string;

export declare const otherTheme: (theme: string) => 'dark' | 'light';
/** The lens host's clip: a circle of `r` px centred on (x, y). */
export declare const lensClip: (x: number, y: number, r?: number) => string;
/** The rim's offset, so its box is centred on (x, y). */
export declare const rimTransform: (x: number, y: number, r?: number) => string;

export interface ThemeLens {
  host: HTMLElement;
  rim: HTMLElement;
  /** Moves the circle: a clip and a transform, nothing rebuilt. */
  move(x: number, y: number): void;
  close(): void;
}

/** Builds the page copy in `theme` once and shows it through a circle at (x, y). */
export declare const openThemeLens: (x: number, y: number, opts: {
  theme: 'dark' | 'light';
  isPicture?(canvas: HTMLCanvasElement): boolean;
  doc?: Document;
}) => ThemeLens;

/** The drag's hooks; `open` stands in for openThemeLens. A drop or a cancel only closes the lens.
 *  No lens opens under the webcore skin or over a swap in flight. */
export declare const themeLensHooks: (app: DrawingApp, opts?: {
  open?: typeof openThemeLens;
  doc?: Document;
}) => Required<IconDragHooks>;

export declare const wireThemeLens: (toggle: HTMLElement | null, app: DrawingApp | null) => IconDragHandle | null;
