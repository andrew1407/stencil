// Opening a project row: gesture → intent. A plain click is DEFERRED one double-click
// interval so a dblclick can cancel it; past the drag slop the gesture belongs to the
// drag/scroll and any pending open is dropped. Pure — timers and the touch test are injected.

export declare const DOUBLE_CLICK_MS: number;
export declare const DRAG_SLOP_PX: number;

export type OpenGestureType = 'tap' | 'click' | 'dblclick' | 'key';
export interface OpenIntent { confirm: boolean; target: 'here' | 'newtab'; }
export declare const rowOpenIntent: (e?: { type?: OpenGestureType; ctrlKey?: boolean; metaKey?: boolean }) => OpenIntent;

export interface ModifierKeys { ctrlKey?: boolean; metaKey?: boolean; }
export interface OpenGesture {
  click(e?: ModifierKeys): void;
  dblclick(e?: ModifierKeys): void;
  key(e?: ModifierKeys): void;
  pressStart(pt?: { x?: number; y?: number }): void;
  /** true the first time the press travels past the slop (the open is dropped). */
  pressMove(pt?: { x?: number; y?: number }): boolean;
  /** true when the press had moved. */
  pressEnd(): boolean;
  dragStart(): void;
  cancel(): void;
  readonly pendingClick: boolean;
  readonly dragging: boolean;
}
export declare const createOpenGesture: (opts?: {
  run?: (intent: OpenIntent) => void;
  touch?: () => boolean;
  delay?: number;
  slop?: number;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (id: unknown) => void;
}) => OpenGesture;

/** Whether an out-of-band change may rebuild the list now: never mid-drag or mid-removal. */
export declare const canRefreshList: (state?: { open?: boolean; dragging?: boolean; removing?: boolean }) => boolean;
