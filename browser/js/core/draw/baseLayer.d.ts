// The picture under the lines: one cached, filtered copy of the cropped/turned image per
// (image, filter, tint) that every frame composites; a duotone tint change of the picture on
// screen is painted by the image worker while the frame keeps the tint it already showed.
import type { FilterLayer } from '../image/filterCanvas.js';

export type BaseSource = CanvasImageSource & { width: number; height: number };

export declare class BaseLayer {
  /** `onReady` repaints once a worker copy lands. */
  constructor(onReady?: () => void);
  onReady: () => void;
  /** On this thread, now — the export and thumbnail path; `image` itself when unfiltered. */
  now(image: BaseSource, filter: string, color: string | null): BaseSource | FilterLayer;
  /** What a frame composites: the exact copy, the previous tint while the worker paints a new one of the same pixel filter, or built now — a filter switch always is. */
  frame(image: BaseSource, filter: string, color: string | null): BaseSource | FilterLayer;
}
