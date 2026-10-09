export declare const DRAG_SLOP_PX: number;
export declare const GHOST_OPACITY: number;
export declare const DRAGGING_CLASS: string;
export declare const SOURCE_CLASS: string;
/** On the ghost while it follows the pointer: its rim shines (css/animations/icon/drag.css). */
export declare const GHOST_CLASS: string;
export declare const TARGET_CLASS: string;
export declare const TARGET_OVER_CLASS: string;

/** Drags started this session: a deferred open notes it and stands down if a drag began since. */
export declare function dragsStarted(): number;

/** Runs `fn` now, or after the drop of the drag in progress. */
export declare function afterIconDrag(fn: () => void): void;

export interface IconDragPoint {
  x: number;
  y: number;
  /** Back over the control the drag left: a release here cancels. */
  overOrigin: boolean;
  /** The element under the pointer (the ghost takes no pointer). */
  target: Element | null;
}

export interface IconDragStart {
  x: number;
  y: number;
  from: { x: number; y: number };
  event: PointerEvent | null;
}

export interface IconDragHooks {
  /** The first move past the slop; `false` refuses the drag and the press stays a press. */
  start?(p: IconDragStart): boolean | void;
  move?(p: IconDragPoint): void;
  /** Released away from the origin. */
  drop?(p: IconDragPoint): void;
  /** Released over the origin, Escape, or the pointer lost. */
  cancel?(): void;
}

export interface IconDragMachine {
  readonly active: boolean;
  press(x: number, y: number, event?: PointerEvent | null): void;
  move(x: number, y: number): boolean;
  release(x: number, y: number): boolean;
  abort(): boolean;
}

export declare function createIconDrag(opts: IconDragHooks & {
  originRect(): { left: number; top: number; right: number; bottom: number };
  targetAt?(x: number, y: number): Element | null;
  slop?: number;
}): IconDragMachine;

export interface IconDragHandle {
  readonly active: boolean;
  abort(): void;
}

/** Wires the machine to `el`: a ghost follows the pointer unless `ghost: false`. */
export declare function wireIconDrag(el: HTMLElement, hooks?: IconDragHooks & {
  ghost?: boolean;
  /** The ghost rides centred on the pointer, not held where it was grabbed. */
  ghostCentred?: boolean;
  enabled?(): boolean;
}): IconDragHandle | null;

/** The side of the square a release point stands for as a flight's origin. */
export declare const DROP_ANCHOR_PX: number;

/** A client rect `size` px square centred on (x, y): where a window dropped there flies out of. */
export declare function dropAnchor(x: number, y: number, size?: number): {
  left: number; top: number; right: number; bottom: number; width: number; height: number;
};

export declare function markDropTarget(el: Element | null, on: boolean, over?: boolean): void;
