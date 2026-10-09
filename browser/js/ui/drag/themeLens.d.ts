// Dragging the theme switch: a lens on the pointer previews the page in the other theme; the drag
// ending anywhere, or Escape, closes it and changes nothing.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { IconDragHandle, IconDragHooks } from './iconDrag.js';

export declare const LENS_RADIUS_PX: number;
/** ms the circle takes to open from a point to LENS_RADIUS_PX, and to close back into one. */
export declare const LENS_GROW_MS: number;
export declare const LENS_CLASS: string;
export declare const RIM_CLASS: string;

export declare const otherTheme: (theme: string) => 'dark' | 'light';
/** The lens host's clip: a circle of `r` px centred on (x, y). */
export declare const lensClip: (x: number, y: number, r?: number) => string;
/** The rim's offset and scale, so its full-radius box is centred on (x, y) at radius `r`. */
export declare const rimTransform: (x: number, y: number, r?: number) => string;
/** The radius `ms` after opening: eased out from 0 to LENS_RADIUS_PX over LENS_GROW_MS. */
export declare const lensRadiusAt: (ms: number) => number;
/** The radius `ms` into closing from `from` px: the same ease back down to 0. */
export declare const closingRadiusAt: (ms: number, from?: number) => number;

export interface ThemeLens {
  host: HTMLElement;
  rim: HTMLElement;
  /** The circle's radius now, px: growing just after it opens. */
  readonly radius: number;
  /** Moves the circle: a clip and a transform, nothing rebuilt. */
  move(x: number, y: number): void;
  /** Shrinks the circle back into a point, then removes it (at once when still). */
  close(): void;
}

/** Builds the page copy in `theme` once and shows it through a circle at (x, y). */
export declare const openThemeLens: (x: number, y: number, opts: {
  theme: 'dark' | 'light';
  isPicture?(canvas: HTMLCanvasElement): boolean;
  doc?: Document;
  /** Open whole at once (default: when motion is reduced). */
  still?: boolean;
  raf?(fn: () => void): unknown;
  now?(): number;
}) => ThemeLens;

/** The drag's hooks; `open` stands in for openThemeLens. A drop or a cancel only closes the lens.
 *  No lens opens under the webcore skin or over a swap in flight. */
export declare const themeLensHooks: (app: DrawingApp, opts?: {
  open?: typeof openThemeLens;
  doc?: Document;
}) => Required<IconDragHooks>;

export declare const wireThemeLens: (toggle: HTMLElement | null, app: DrawingApp | null) => IconDragHandle | null;
