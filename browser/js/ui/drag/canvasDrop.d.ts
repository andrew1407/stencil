import type { IconDragHandle, IconDragHooks } from './iconDrag.js';

export interface CanvasDropOptions {
  /** The canvas frame, resolved as the drag starts; null refuses the drag. */
  frame(): Element | null;
  /** Runs once, when the release lands on the frame. */
  act(): void;
}

/** The drag hooks: the frame glows while live, brighter under the pointer; a release on it acts. */
export declare function canvasDropHooks(opts: CanvasDropOptions): Required<IconDragHooks>;

/** Wires `btn` so a drop on the canvas runs `act`; null without a control. */
export declare function wireCanvasDrop(btn: HTMLElement | null, opts: CanvasDropOptions): IconDragHandle | null;
